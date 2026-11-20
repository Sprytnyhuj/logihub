/*
 * LogiHub Voice FX - redukcja szumów RNNoise (sieć neuronowa, xiph.org) jako wtyczka LADSPA.
 * RNNoise pracuje na ramkach 480 próbek przy 48 kHz; wtyczka buforuje, więc dodaje 10 ms opóźnienia.
 * Przy innej częstotliwości próbkowania sygnał przechodzi bez zmian.
 *
 *   logihub_rnnoise   porty: 0 In, 1 Out, 2 "VAD threshold (%)" (0 = sama redukcja szumów)
 */
#include <ladspa.h>
#include <rnnoise.h>
#include <stdlib.h>
#include <string.h>

#define FRAME 480

typedef struct {
    LADSPA_Data *in, *out, *vad;
    DenoiseState *st;
    float ibuf[FRAME], obuf[FRAME];
    int pos;
    int bypass;
} Rn;

static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
    (void)d;
    Rn *r = calloc(1, sizeof(Rn));
    if (!r)
        return NULL;
    r->bypass = rate != 48000;
    if (!r->bypass && !(r->st = rnnoise_create(NULL))) {
        free(r);
        return NULL;
    }
    return r;
}

static void connect_port(LADSPA_Handle h, unsigned long p, LADSPA_Data *data)
{
    Rn *r = h;
    if (p == 0) r->in = data;
    else if (p == 1) r->out = data;
    else if (p == 2) r->vad = data;
}

static void activate(LADSPA_Handle h)
{
    Rn *r = h;
    r->pos = 0;
    memset(r->obuf, 0, sizeof r->obuf);
}

static void run(LADSPA_Handle h, unsigned long n)
{
    Rn *r = h;
    if (r->bypass) {
        if (r->out != r->in)
            memmove(r->out, r->in, n * sizeof(float));
        return;
    }
    const float thr = r->vad ? *r->vad / 100.0f : 0.0f;
    for (unsigned long i = 0; i < n; i++) {
        const float x = r->in[i];               /* odczyt przed zapisem - działa też dla in == out */
        r->out[i] = r->obuf[r->pos] / 32768.0f; /* wynik poprzedniej ramki (opóźnienie 1 ramki) */
        r->ibuf[r->pos] = x * 32768.0f;         /* RNNoise oczekuje skali 16-bit */
        if (++r->pos == FRAME) {
            const float voice = rnnoise_process_frame(r->st, r->obuf, r->ibuf);
            if (voice < thr)
                memset(r->obuf, 0, sizeof r->obuf);
            r->pos = 0;
        }
    }
}

static void cleanup(LADSPA_Handle h)
{
    Rn *r = h;
    if (r->st)
        rnnoise_destroy(r->st);
    free(r);
}

static const LADSPA_PortDescriptor kPd[] = {LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
                                            LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL};
static const char *const kPn[] = {"In", "Out", "VAD threshold (%)"};
static const LADSPA_PortRangeHint kPh[] = {
    {0, 0, 0}, {0, 0, 0}, {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MINIMUM, 0, 99}};

static const LADSPA_Descriptor kDesc = {
    .UniqueID = 0x4C480010,
    .Label = "logihub_rnnoise",
    .Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,
    .Name = "LogiHub RNNoise",
    .Maker = "LogiHub (RNNoise by xiph.org)",
    .Copyright = "MIT / BSD-3-Clause (RNNoise)",
    .PortCount = 3,
    .PortDescriptors = kPd,
    .PortNames = kPn,
    .PortRangeHints = kPh,
    .instantiate = instantiate,
    .connect_port = connect_port,
    .activate = activate,
    .run = run,
    .cleanup = cleanup,
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    return index == 0 ? &kDesc : NULL;
}
