DTS := SPI0.dts
DTBO := $(DTS:.dts=.dtbo)

all: $(DTBO)

%.dtbo: %.dts
	dtc -@ -I dts -O dtb -o $@ $<

clean:
	rm -f $(DTBO)
