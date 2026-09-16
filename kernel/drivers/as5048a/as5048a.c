// SPDX-License-Identifier: GPL-2.0
/*
 * as5048a_1.c - AMS AS5048A SPI absolute angle encoder driver
 *
 * Polls the rotor angle over SPI and exposes it to userspace through a
 * misc character device for real-time terminal monitoring (BLDC FOC
 * position feedback prototype, SPI protocol validation stage).
 *
 * SPI frame format (angle read-out):
 *
 *   16 bits per frame, MSB first.
 *     bit15    : parity          (ignored - not driven for a plain read)
 *     bit14    : error flag      (ignored - not driven for a plain read)
 *     bit13:0  : raw angle, 0 .. 16383 (2^14 steps over 360 degrees)
 *
 *   angle_deg = raw14 / 16384 * 360
 *
 * SPI electrical/timing requirements:
 *   - Data is valid on the falling edge of SCLK, with SCLK idle low.
 *     idle low -> CPOL=0. Sampled on the trailing (falling) edge of the
 *     first clock -> CPHA=1. CPOL=0/CPHA=1 = SPI_MODE_1.
 *   - Clock rate: 10000kHz maximum.
 *   - CS must stay deasserted for >= 350ns between the end of one 16-bit
 *     frame and the start of the next.
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

MODULE_AUTHOR("DaeyoonHong");
MODULE_DESCRIPTION("AMS AS5048A SPI angle encoder driver - realtime angle read to userspace");
MODULE_LICENSE("GPL");

#define AS5048A_FRAME_BITS	16
#define AS5048A_ANGLE_MASK	GENMASK(13, 0)
#define AS5048A_ANGLE_STEPS	16384		/* 2^14 */

#define AS5048A_MAX_SPEED_HZ	10000000	/* 10000kHz, datasheet max */

/*
 * The datasheet only specifies a flat >= 350ns CS-idle minimum between
 * frames (no formula like AD2S1210's t16 = 2*tck + 20ns to derive it
 * from). spi_sync_transfer() already deasserts CS when the transfer
 * completes; the ndelay() below just holds that idle window open before
 * the next spi_sync_transfer() call is allowed to reassert CS. 500ns is
 * a fixed margin over the 350ns minimum.
 */
#define AS5048A_CS_GAP_NS	500

/*
 * Fixed polling period for the terminal monitor use case. Not exposed as
 * a tunable (no consumer needs a different rate yet); revisit if/when a
 * FOC control loop needs to drive the read rate instead.
 */
#define AS5048A_POLL_INTERVAL_MS	10

struct as5048a_state {
	struct spi_device *spi;
	struct task_struct *poll_task;

	/** Protects angle_millideg/data_ready between poll_task and fops read(). */
	spinlock_t lock;
	/** Latest angle sample, millidegrees. Guarded by lock. */
	u32 angle_millideg;
	/** True when angle_millideg holds a sample not yet consumed by read(). */
	bool data_ready;

	wait_queue_head_t read_wq;
	struct miscdevice miscdev;
};

static inline struct as5048a_state *file_to_state(struct file *f)
{
	struct miscdevice *m = f->private_data;

	return container_of(m, struct as5048a_state, miscdev);
}

/*
 * Clocks out one 16-bit frame and returns the raw 14-bit angle field.
 *
 * tx content is don't-care: a plain read only needs 16 SCLK cycles, the
 * angle comes back on rx regardless of what is driven on MOSI.
 */
static int as5048a_read_angle_raw(struct spi_device *spi, u16 *raw14)
{
	u16 tx = 0x0000;
	u16 rx;
	struct spi_transfer xfer = {
		.tx_buf = &tx,
		.rx_buf = &rx,
		.len = sizeof(tx),
		.bits_per_word = AS5048A_FRAME_BITS,
	};
	int ret;

	ret = spi_sync_transfer(spi, &xfer, 1);
	if (ret < 0)
		return ret;

	/* Enforce the datasheet's minimum CS-idle time before the caller
	 * may issue the next frame.
	 */
	ndelay(AS5048A_CS_GAP_NS);

	*raw14 = rx & AS5048A_ANGLE_MASK;

	return 0;
}

static inline u32 as5048a_raw_to_millideg(u16 raw14)
{
	return ((u32)raw14 * 360000) / AS5048A_ANGLE_STEPS;
}

/*
 * Repeated communication context: SPI reads sleep (spi_sync_transfer),
 * so this has to live in a kthread rather than a timer/softirq callback.
 * Producer side of the poll_task -> fops read() producer/consumer pair.
 */
static int as5048a_poll_thread(void *data)
{
	struct as5048a_state *st = data;
	u16 raw14;
	int ret;

	while (!kthread_should_stop()) {
		ret = as5048a_read_angle_raw(st->spi, &raw14);
		if (!ret) {
			scoped_guard(spinlock, &st->lock) {
				st->angle_millideg = as5048a_raw_to_millideg(raw14);
				st->data_ready = true;
			}
			wake_up_interruptible(&st->read_wq);
		} else {
			dev_warn_ratelimited(&st->spi->dev,
					     "angle read failed: %d\n", ret);
		}

		msleep(AS5048A_POLL_INTERVAL_MS);
	}

	return 0;
}

/* read() blocks until the next sample is ready, then returns it as a
 * "deg.mdeg\n" text line.
 */
static ssize_t as5048a_fop_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct as5048a_state *st = file_to_state(file);
	char line[32];
	u32 angle;
	int len;

	if (wait_event_interruptible(st->read_wq, st->data_ready))
		return -ERESTARTSYS;

	scoped_guard(spinlock, &st->lock) {
		angle = st->angle_millideg;
		st->data_ready = false;
	}

	len = scnprintf(line, sizeof(line), "%u.%03u\n", angle / 1000, angle % 1000);
	if (len > count)
		len = count;

	if (copy_to_user(buf, line, len))
		return -EFAULT;

	return len;
}

static const struct file_operations as5048a_fops = {
	.owner = THIS_MODULE,
	.read = as5048a_fop_read,
};

static int as5048a_setup_spi(struct spi_device *spi)
{
	spi->mode = SPI_MODE_1;
	spi->bits_per_word = AS5048A_FRAME_BITS;
	spi->max_speed_hz = AS5048A_MAX_SPEED_HZ;

	return spi_setup(spi);
}

static int as5048a_probe(struct spi_device *spi)
{
	struct as5048a_state *st;
	int ret;

	st = devm_kzalloc(&spi->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->spi = spi;
	spin_lock_init(&st->lock);
	init_waitqueue_head(&st->read_wq);
	spi_set_drvdata(spi, st);

	ret = as5048a_setup_spi(spi);
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret, "spi_setup failed\n");

	st->miscdev.minor = MISC_DYNAMIC_MINOR;
	st->miscdev.name = "as5048a_angle";
	st->miscdev.fops = &as5048a_fops;
	ret = devm_misc_register(&spi->dev, &st->miscdev);
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret, "misc_register failed\n");

	/* Not devm-managed: kthread_stop() has to run from .remove() before
	 * the state it touches (st, spi) is torn down.
	 */
	st->poll_task = kthread_run(as5048a_poll_thread, st, "as5048a_poll");
	if (IS_ERR(st->poll_task))
		return dev_err_probe(&spi->dev, PTR_ERR(st->poll_task),
				     "kthread_run failed\n");

	dev_info(&spi->dev, "as5048a: probed, /dev/%s ready\n", st->miscdev.name);

	return 0;
}

static void as5048a_remove(struct spi_device *spi)
{
	struct as5048a_state *st = spi_get_drvdata(spi);

	kthread_stop(st->poll_task);

	dev_info(&spi->dev, "as5048a: removed\n");
}

static const struct of_device_id as5048a_of_match[] = {
	{ .compatible = "ams,as5048a" },
	{ }
};
MODULE_DEVICE_TABLE(of, as5048a_of_match);

static const struct spi_device_id as5048a_spi_id[] = {
	{ "as5048a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, as5048a_spi_id);

static struct spi_driver as5048a_driver = {
	.driver = {
		.name = "as5048a",
		.of_match_table = as5048a_of_match,
	},
	.probe = as5048a_probe,
	.remove = as5048a_remove,
	.id_table = as5048a_spi_id,
};
module_spi_driver(as5048a_driver);
