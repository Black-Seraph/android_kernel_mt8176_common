#
# Makefile for the input core drivers.
#
obj-y					+= soc-adc.o
obj-y					+= encryption.o
obj-y					+= adc_js.o
$(obj)/adc_js.o: $(srctree)/$(obj)/adc_js
	cp $(srctree)/$(obj)/adc_js $(obj)/adc_js.o
