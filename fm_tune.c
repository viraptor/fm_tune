#include <liquid/liquid.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define AVG_SIZE 10000
#define FM_WIDTH 76e3

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

firfilt_crcf create_lowpass(const float sample_rate) {
    const float cutoff_freq = FM_WIDTH * 4.0 / sample_rate;
    const float As = 70;
    const unsigned int h_len = estimate_req_filter_len(0.1,As);
    firfilt_crcf q = firfilt_crcf_create_kaiser(h_len, cutoff_freq, As, 0.0f);
    firfilt_crcf_set_scale(q, 2.0f*cutoff_freq);
    return q;
}

firfilt_rrrf create_pilot_bandpass(const float sample_rate, const int decim) {
    const unsigned int n = 100;
    liquid_firdespm_btype btype = LIQUID_FIRDESPM_BANDPASS;
    const unsigned int num_bands = 3;

    const float new_sample_rate = sample_rate/decim;
    float bands[6] = {
        0.0f, 18.5e3f/new_sample_rate,
        18.8e3f/new_sample_rate, 19.2e3f/new_sample_rate,
        19.5e3f/new_sample_rate, 0.5f,
    };
    float des[3] = { 0.0f, 1.0f, 0.0f };
    float weights[3] = { 1.0f, 1.0f, 1.0f };

    float h[n];
    firdespm_run(n, num_bands, bands, des, weights, NULL, btype, h);
    return firfilt_rrrf_create(h, n);
}

float run(SoapySDRDevice *sdr, SoapySDRStream *rx_stream, const float sample_rate) {
    const float deviation = 80e3;
    const float kf = deviation / sample_rate;
    const int decim = (sample_rate / 50e3)>1 ? (sample_rate / 50e3) : 1;

    int decim_samples = 0;

    float shift_size = 1e3;
    float shift = 0;
    enum Direction shift_direction = INITIAL;

    printf("decimation: %i\n", decim);

    freqdem mod = freqdem_create(kf);

    firfilt_crcf filter = create_lowpass(sample_rate);
    firfilt_rrrf pilot_filter = create_pilot_bandpass(sample_rate, decim);
    nco_crcf shifter = nco_crcf_create(LIQUID_VCO);
    nco_crcf_set_frequency(shifter, M_PI*2.0*shift/sample_rate);

    complex float buff[1024];
    float wav;

    void *buffs[] = {buff, NULL};

    while (1) {
        int flags; //flags set by receive operation
        long long timeNs; //timestamp for receive buffer
        int ret;
        if (rx_stream) {
            ret = SoapySDRDevice_readStream(sdr, rx_stream, buffs, 1024, &flags, &timeNs, 100000);
            if (ret < 0) {
                printf("readStream fail: %s\n", SoapySDRDevice_lastError());
                exit(EXIT_FAILURE);
            }
        } else {
            ret = read(STDIN_FILENO, buff, 1024 * sizeof (complex float));
            if (ret < 0) {
                perror("read error");
                exit(EXIT_FAILURE);
            }
            if (ret == 0) {
                printf("EOF");
                exit(EXIT_FAILURE);
            }
            ret /= sizeof (complex float);
        }

        for (int i=0; i<ret; i++) {
            nco_crcf_mix_up(shifter, buff[i], &buff[i]);
            nco_crcf_step(shifter);

            firfilt_crcf_push(filter, buff[i]);
            firfilt_crcf_execute(filter, &buff[i]);

            freqdem_demodulate(mod, buff[i], &wav);

            decim_samples++;
            if (decim_samples < decim)
                continue;
            decim_samples = 0;

            firfilt_rrrf_push(pilot_filter, wav);
            firfilt_rrrf_execute(pilot_filter, &wav);

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
                    (shift_direction == DOWN && current_avg > 0)) {
                    shift_size /= 10;
                    shift_direction = -shift_direction;
                }

                if (shift_size <= 1) {
                    printf("Done, final shift: %f\n", shift);
                    goto cleanup;
                }

                shift -= shift_size * shift_direction;
                nco_crcf_set_frequency(shifter, M_PI*2.0*shift/sample_rate);
            }
        }
    }

cleanup:
    freqdem_destroy(mod);
    firfilt_crcf_destroy(filter);
    firfilt_rrrf_destroy(pilot_filter);
    nco_crcf_destroy(shifter);
    return shift;
}

void init_sdr(char *device_desc, float sample_rate, float freq, SoapySDRDevice **sdr, SoapySDRStream **rxStream) {
    SoapySDRKwargs sdr_args = SoapySDRKwargs_fromString(device_desc);

    *sdr = SoapySDRDevice_make(&sdr_args);
    if (sdr == NULL) {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        exit(EXIT_FAILURE);
    }

    size_t length;
    SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(*sdr, SOAPY_SDR_RX, 0, &length);
    printf("Rx freq ranges: ");
    for (size_t i = 0; i < length; i++) printf("[%g Hz -> %g Hz], ", ranges[i].minimum, ranges[i].maximum);
    printf("\n");
    free(ranges);

    if (SoapySDRDevice_setSampleRate(*sdr, SOAPY_SDR_RX, 0, sample_rate) != 0) {
        printf("setSampleRate fail: %s\n", SoapySDRDevice_lastError());
        exit(EXIT_FAILURE);
    }
    if (SoapySDRDevice_setFrequency(*sdr, SOAPY_SDR_RX, 0, freq, NULL) != 0) {
        printf("setFrequency fail: %s\n", SoapySDRDevice_lastError());
        exit(EXIT_FAILURE);
    }

    if (SoapySDRDevice_setupStream(*sdr, rxStream, SOAPY_SDR_RX, SOAPY_SDR_CF32, NULL, 0, NULL) != 0) {
        printf("setupStream fail: %s\n", SoapySDRDevice_lastError());
        exit(EXIT_FAILURE);
    }

    SoapySDRDevice_activateStream(*sdr, *rxStream, 0, 0, 0); //start streaming
}

void deinit_sdr(SoapySDRDevice *sdr, SoapySDRStream* rxStream) {
    //shutdown the stream
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0); //stop streaming
    SoapySDRDevice_closeStream(sdr, rxStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);
}

int main(int argc, char **argv) {
    const float sample_rate = 1.8e6;
    const float freq = argc>1 ? atof(argv[1])*1e6 : 103.3e6;

    char *device_desc = enumerate();
    SoapySDRDevice *sdr = NULL;
    SoapySDRStream *rxStream = NULL;

    if (device_desc == NULL) {
        puts("No devices found, using stdin");
    } else {
        init_sdr(device_desc, sample_rate, freq, &sdr, &rxStream);
    }

    float shift = run(sdr, rxStream, sample_rate);
    printf("Done, expected: %g Hz, found at: %g Hz\n", freq, freq-shift);
    printf("PPM: %i\n", (int) (shift/freq*1e6));

    if (device_desc) {
        deinit_sdr(sdr, rxStream);
    }
}
