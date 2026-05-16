#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "ft8/message.h"
#include "ft8/encode.h"
#include "ft8/constants.h"

#define SAMPLE_RATE 12000
#define SYMBOL_COUNT FT8_NN
#define SYMBOL_DURATION 0.16f
#define BASE_FREQ 1000.0f
#define TONE_SPACING 6.25f
#define MAX_MSG_LEN 128

static void usage(const char* prog)
{
    printf("Uso:\n");
    printf("  %s \"CQ XE2ABC EL50\"\n", prog);
    printf("  %s            (modo interactivo)\n", prog);
}

void my_ft8_encode(const char* msg, uint8_t* symbols)
{
    ftx_message_t packed;
    ftx_message_rc_t rc = ftx_message_encode(&packed, NULL, msg);
    if (rc != FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "Error: mensaje FT8 invalido (rc=%d)\n", (int)rc);
        exit(2);
    }

    ft8_encode(packed.payload, symbols);
}

void generate_ft8_audio(const uint8_t* symbols, float* buffer, int* sample_count)
{
    const int samples_per_symbol = 1920; // 12000 * 0.16
    const int silence_samples = SAMPLE_RATE; // 1 second
    const int signal_samples = SYMBOL_COUNT * samples_per_symbol;
    const int total_samples = signal_samples + 2 * silence_samples;
    const float amplitude = 0.5f;
    const float alpha = 0.03f; // smoothing factor for soft tone transitions

    for (int i = 0; i < total_samples; ++i)
    {
        buffer[i] = 0.0f;
    }

    float phase = 0.0f;
    float current_freq = BASE_FREQ + (float)symbols[0] * TONE_SPACING;

    for (int i = 0; i < SYMBOL_COUNT; ++i)
    {
        const float target_freq = BASE_FREQ + (float)symbols[i] * TONE_SPACING;
        for (int n = 0; n < samples_per_symbol; ++n)
        {
            current_freq += alpha * (target_freq - current_freq);
            phase += 2.0f * (float)M_PI * current_freq / (float)SAMPLE_RATE;
            if (phase >= 2.0f * (float)M_PI)
            {
                phase = fmodf(phase, 2.0f * (float)M_PI);
            }

            const int idx = silence_samples + i * samples_per_symbol + n;
            buffer[idx] = amplitude * sinf(phase);
        }
    }

    *sample_count = total_samples;
}

void write_wav(const char* filename, float* buffer, int sample_count)
{
    FILE* f = fopen(filename, "wb");
    if (!f)
    {
        fprintf(stderr, "Error: no se pudo abrir %s\n", filename);
        exit(3);
    }

    const int16_t bits_per_sample = 16;
    const int16_t channels = 1;
    const int32_t byte_rate = SAMPLE_RATE * channels * (bits_per_sample / 8);
    const int16_t block_align = channels * (bits_per_sample / 8);
    const int32_t data_size = sample_count * block_align;
    const int32_t riff_size = 36 + data_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    int32_t fmt_size = 16;
    int16_t audio_format = 1;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&(int32_t){SAMPLE_RATE}, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);

    for (int i = 0; i < sample_count; ++i)
    {
        float s = buffer[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t pcm = (int16_t)lrintf(s * 32767.0f);
        fwrite(&pcm, sizeof(int16_t), 1, f);
    }

    fclose(f);
}

int main(int argc, char** argv)
{
    char input[MAX_MSG_LEN + 1] = {0};
    const char* msg = NULL;

    if (argc >= 2)
    {
        msg = argv[1];
    }
    else
    {
        printf("Ingresa mensaje FT8: ");
        if (!fgets(input, sizeof(input), stdin))
        {
            fprintf(stderr, "Error leyendo entrada\n");
            return 1;
        }
        input[strcspn(input, "\r\n")] = '\0';
        msg = input;
    }

    if (!msg || msg[0] == '\0')
    {
        fprintf(stderr, "Error: mensaje vacio\n");
        usage(argv[0]);
        return 1;
    }

    if (strlen(msg) > MAX_MSG_LEN)
    {
        fprintf(stderr, "Error: mensaje demasiado largo (max %d)\n", MAX_MSG_LEN);
        return 1;
    }

    uint8_t symbols[SYMBOL_COUNT];
    my_ft8_encode(msg, symbols);

    printf("Simbolos FT8 (%d):\n", SYMBOL_COUNT);
    for (int i = 0; i < SYMBOL_COUNT; ++i)
    {
        printf("%d", symbols[i]);
        if (i < SYMBOL_COUNT - 1) printf(" ");
    }
    printf("\n");

    const int max_samples = SYMBOL_COUNT * 1920 + 2 * SAMPLE_RATE;
    float* audio = (float*)malloc(sizeof(float) * (size_t)max_samples);
    if (!audio)
    {
        fprintf(stderr, "Error: memoria insuficiente\n");
        return 1;
    }

    int sample_count = 0;
    generate_ft8_audio(symbols, audio, &sample_count);
    write_wav("output.wav", audio, sample_count);

    printf("WAV generado: output.wav (%d muestras @ %d Hz)\n", sample_count, SAMPLE_RATE);

    free(audio);
    return 0;
}
