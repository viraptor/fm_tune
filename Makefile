SOAPY_LDFLAGS?=$(shell (pkg-config --exists SoapySDR && pkg-config --libs SoapySDR) || echo '-lSoapySDR')
SOAPY_CFLAGS?=$(shell (pkg-config --exists SoapySDR && pkg-config --cflags SoapySDR) || echo '')
LDFLAGS+=-lliquid -lm $(SOAPY_LDFLAGS)
CFLAGS+=-std=c99 -O3 -Wall -Wextra -march=native $(SOAPY_CFLAGS)

TARGET = fm_tune

$(TARGET): $(TARGET).c
	$(CC) $(TARGET).c $(CFLAGS) $(LDFLAGS) -o $(TARGET)

all: $(TARGET)

clean:
	$(RM) $(TARGET)
