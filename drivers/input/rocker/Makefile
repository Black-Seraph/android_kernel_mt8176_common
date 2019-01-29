#
# Makefile for the input core drivers.
#

#obj-$(CONFIG_SOC_ADC)			+= soc-adc.o
#obj-$(CONFIG_ADC_JS)			+= adc_js.o
obj-y					+= soc-adc.o
obj-y					+= encrytion.o
obj-y					+= adc_js.o
$(obj)/adc_js.o: $(srctree)/$(obj)/adc_js	
	cp $(srctree)/$(obj)/adc_js $(obj)/adc_js.o
