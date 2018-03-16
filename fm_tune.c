#include <liquid/liquid.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#define AVG_SIZE 500000

enum Direction {
    INITIAL = 0,
    DOWN = -1,
    UP = 1,
};

float current_avg = 0;
char update_avg(float val) {
    static float avg_table[AVG_SIZE] = {0};
    static int next_pos = 0;

    current_avg += val / AVG_SIZE - avg_table[next_pos]/AVG_SIZE;
    avg_table[next_pos] = val;
    next_pos++;
    if (next_pos == AVG_SIZE) {
        next_pos = 0;
        current_avg = 0;
        for(int i=0; i<AVG_SIZE; i++) {
            current_avg += avg_table[i];
        }
        current_avg /= AVG_SIZE;
    }
    return next_pos == 0;
}

char *enumerate() {
    char *to_return=NULL;

    size_t length;
    SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
    for (size_t i = 0; i < length; i++)
    {
        printf("Found device #%d: ", (int)i);
        if (i==0) {
            to_return = SoapySDRKwargs_toString(&results[i]);
        }
        for (size_t j = 0; j < results[i].size; j++)
        {
            printf("%s=%s, ", results[i].keys[j], results[i].vals[j]);
        }
        printf("\n");
    }
    SoapySDRKwargsList_clear(results, length);

    return to_return;
}

float run(SoapySDRDevice *sdr, SoapySDRStream *rx_stream, const float sample_rate) {
    const float deviation = 80e3;
    const float kf = deviation / sample_rate;

    float shift_size = 1e3;
    float shift = 0;
    enum Direction shift_direction = INITIAL;

    freqdem mod = freqdem_create(kf);

    float t=0;
    complex float buff[1024];
    float wav;

    void *buffs[] = {buff, NULL};

    while (1) {
        int flags; //flags set by receive operation
        long long timeNs; //timestamp for receive buffer
        int ret = SoapySDRDevice_readStream(sdr, rx_stream, buffs, 1024, &flags, &timeNs, 100000);
        if (ret < 0) {
            printf("readStream fail: %s\n", SoapySDRDevice_lastError());
            exit(EXIT_FAILURE);
        }

        for (int i=0; i<ret; i++) {
            buff[i] *= cexpf(2.0J*M_PI*t*shift);
            t += 1./sample_rate;
            
            freqdem_demodulate(mod, buff[i], &wav);

            if (update_avg(wav)) {
                printf("Current avg: %f, shift: %f, scale: %f\n", current_avg, shift, shift_size);
                if (shift_direction == INITIAL) {
                    if (current_avg > 0) {
                        shift_direction = UP;
                    } else {
                        shift_direction = DOWN;
                    }
                }

                if ((shift_direction == UP && current_avg < 0) ||
                    (shift_direction == DOWN &&current_avg > 0)) {
                    shift_size /= 10;
                    shift_direction = -shift_direction;
                }

                if (shift_size <= 1) {
                    printf("Done, final shift: %f\n", shift);
                    return shift;
                }

                if (shift_direction == UP) {
                    shift -= shift_size;
                } else {
                    shift += shift_size;
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    const float sample_rate = 1.8e6;
    const float freq = argv[1] ? atof(argv[1]) : 103.3e6;

    char *device_desc = enumerate();
    if (device_desc == NULL) {
        printf("No devices found\n");
        exit(EXIT_FAILURE);
    }
    SoapySDRKwargs sdr_args = SoapySDRKwargs_fromString(device_desc);
    free(device_desc);

    SoapySDRDevice *sdr = SoapySDRDevice_make(&sdr_args);
    if (sdr == NULL) {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    size_t length;
    SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_RX, 0, &length);
    printf("Rx freq ranges: ");
    for (size_t i = 0; i < length; i++) printf("[%g Hz -> %g Hz], ", ranges[i].minimum, ranges[i].maximum);
    printf("\n");
    free(ranges);

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate) != 0) {
        printf("setSampleRate fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, freq, NULL) != 0) {
        printf("setFrequency fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    SoapySDRStream *rxStream;
    if (SoapySDRDevice_setupStream(sdr, &rxStream, SOAPY_SDR_RX, SOAPY_SDR_CF32, NULL, 0, NULL) != 0) {
        printf("setupStream fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }
    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); //start streaming

    float shift = run(sdr, rxStream, sample_rate);
    printf("Done, expected: %g Hz, found at: %g Hz\n", freq, freq-shift);
    printf("PPM: %i\n", (int) (shift/freq*1e6));

    //shutdown the stream
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0); //stop streaming
    SoapySDRDevice_closeStream(sdr, rxStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);
}