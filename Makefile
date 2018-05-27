SOAPY_LDFLAGS?=$(shell (pkg-config --exists SoapySDR && pkg-config --libs SoapySDR) || echo '-lSoapySDR')
SOAPY_CFLAGS?=$(shell (pkg-config --exists SoapySDR && pkg-config --cflags SoapySDR) || echo '')
LDFLAGS+=-lliquid -lm $(SOAPY_LDFLAGS)
CFLAGS+=-std=gnu11 -O3 -Wall -Wextra -march=native $(SOAPY_CFLAGS)

all: fm_tune
