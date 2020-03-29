SOAPY_LDFLAGS?=$(shell (pkg-config --exists SoapySDR && pkg-config --libs SoapySDR) || echo '-lSoapySDR')
SOAPY_CFLAGS?=$(shell (pkg-config --exists SoapySDR && pkg-config --cflags SoapySDR) || echo '')
LDFLAGS+=-lliquid -lm $(SOAPY_LDFLAGS)
CFLAGS+=-std=c99 -O3 -Wall -Wextra -march=native $(SOAPY_CFLAGS)

TARGET_EXEC = fm_tune

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(TARGET).c

clean:
	$(RM) $(TARGET)
