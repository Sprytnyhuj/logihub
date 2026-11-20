/*
 * LogiHub Voice FX - efekty dynamiki jako wtyczka LADSPA (dla modułu filter-chain PipeWire).
 * Bez zależności poza libc/libm.
 *
 *   logihub_gate        bramka szumów z histerezą i płynnym otwieraniem
 *   logihub_compressor  kompresor (detekcja szczytowa, miękkie kolano)
 *   logihub_deesser     dynamiczny de-esser: ścisza tylko pasmo sybilantów, gdy przekroczy próg
 *   logihub_limiter     limiter szczytowy (natychmiastowy atak, płynne zwalnianie)
 */
#include <ladspa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DB2LIN(x) powf(10.0f, (x) / 20.0f)
#define LIN2DB(x) (20.0f * log10f((x) + 1e-9f))

static float coef(float ms, float rate) { return ms <= 0.0f ? 0.0f : expf(-1.0f / (ms * 0.001f * rate)); }

/* ------------------------------------------------------------------ */
typedef struct {
    LADSPA_Data *port[8];
    float rate;
    float env, gain, z1, z2, x1, x2; /* stan */
    int open;
    /* de-esser: filtr półkowy sterowany poziomem sybilantów */
    float sx1, sx2, sy1, sy2;
    float sb0, sb1, sb2, sa1, sa2;
    float red, lastRed;
    unsigned counter;
} Fx;

static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
    (void)d;
    Fx *f = calloc(1, sizeof(Fx));
    if (f) {
        f->rate = (float)rate;
        f->gain = 1.0f;
    }
    return f;
}
static void connect_port(LADSPA_Handle h, unsigned long p, LADSPA_Data *data)
{
    if (p < 8)
        ((Fx *)h)->port[p] = data;
}
static void activate(LADSPA_Handle h)
{
    Fx *f = h;
    f->env = 0;
    f->gain = 1;
    f->z1 = f->z2 = f->x1 = f->x2 = 0;
    f->open = 0;
    f->sx1 = f->sx2 = f->sy1 = f->sy2 = 0;
    f->sb0 = 1;
    f->sb1 = f->sb2 = f->sa1 = f->sa2 = 0;
    f->red = 0;
    f->lastRed = -1;
    f->counter = 0;
}
static void cleanup(LADSPA_Handle h) { free(h); }

/* ---- bramka: porty 0 in, 1 out, 2 open dB, 3 close dB, 4 release ms ---- */
static void run_gate(LADSPA_Handle h, unsigned long n)
{
    Fx *f = h;
    const float open = *f->port[2], close = *f->port[3];
    const float aEnv = coef(1.0f, f->rate), rEnv = coef(60.0f, f->rate);
    const float aG = coef(2.0f, f->rate), rG = coef(*f->port[4], f->rate);
    for (unsigned long i = 0; i < n; i++) {
        const float x = f->port[0][i], a = fabsf(x);
        f->env = a > f->env ? aEnv * f->env + (1 - aEnv) * a : rEnv * f->env + (1 - rEnv) * a;
        const float db = LIN2DB(f->env);
        if (db > open)
            f->open = 1;
        else if (db < close)
            f->open = 0;
        const float target = f->open ? 1.0f : 0.0f;
        const float c = target > f->gain ? aG : rG;
        f->gain = c * f->gain + (1 - c) * target;
        f->port[1][i] = x * f->gain;
    }
}

/* ---- kompresor: 0 in, 1 out, 2 threshold dB, 3 ratio, 4 attack ms, 5 release ms, 6 makeup dB ---- */
static void run_comp(LADSPA_Handle h, unsigned long n)
{
    Fx *f = h;
    const float thr = *f->port[2], ratio = fmaxf(1.0f, *f->port[3]), knee = 6.0f;
    const float att = coef(*f->port[4], f->rate), rel = coef(*f->port[5], f->rate);
    const float makeup = DB2LIN(*f->port[6]);
    for (unsigned long i = 0; i < n; i++) {
        const float x = f->port[0][i], a = fabsf(x);
        f->env = a > f->env ? att * f->env + (1 - att) * a : rel * f->env + (1 - rel) * a;
        const float db = LIN2DB(f->env), over = db - thr;
        float red = 0.0f;
        if (over > knee / 2)
            red = over * (1 - 1 / ratio);
        else if (over > -knee / 2) /* miękkie kolano */
            red = (1 - 1 / ratio) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        f->port[1][i] = x * DB2LIN(-red) * makeup;
    }
}

/* ---- de-esser: 0 in, 1 out, 2 frequency Hz, 3 threshold dB, 4 max reduction dB ----
 * Tor boczny: filtr górnoprzepustowy mierzy poziom sybilantów. Gdy przekroczy próg, filtr półkowy
 * (high-shelf) na sygnale głównym ścisza wysokie pasmo o wyliczoną wartość. Poza syczeniem
 * filtr ma 0 dB, więc głos przechodzi bez zmian. */
static void shelf(Fx *f, float fc, float gainDb)
{
    const float A = powf(10.0f, gainDb / 40.0f);
    const float w = 2.0f * (float)M_PI * fc / f->rate, cw = cosf(w);
    const float al = sinf(w) / 2.0f * sqrtf(2.0f), sA = 2.0f * sqrtf(A) * al;
    const float a0 = (A + 1) - (A - 1) * cw + sA;
    f->sb0 = A * ((A + 1) + (A - 1) * cw + sA) / a0;
    f->sb1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
    f->sb2 = A * ((A + 1) + (A - 1) * cw - sA) / a0;
    f->sa1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
    f->sa2 = ((A + 1) - (A - 1) * cw - sA) / a0;
}

static void run_deess(LADSPA_Handle h, unsigned long n)
{
    Fx *f = h;
    const float fc = fminf(*f->port[2], f->rate * 0.45f);
    const float w = 2.0f * (float)M_PI * fc / f->rate, cw = cosf(w), al = sinf(w) / (2 * 0.707f);
    const float b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2, a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
    const float thr = *f->port[3], maxRed = *f->port[4];
    const float att = coef(0.5f, f->rate), rel = coef(40.0f, f->rate);
    const float smooth = coef(2.0f, f->rate); /* wygładzenie zmian filtra półkowego */
    for (unsigned long i = 0; i < n; i++) {
        const float x = f->port[0][i];
        /* tor boczny */
        const float hp = (b0 * x + b1 * f->x1 + b2 * f->x2 - a1 * f->z1 - a2 * f->z2) / a0;
        f->x2 = f->x1;
        f->x1 = x;
        f->z2 = f->z1;
        f->z1 = hp;
        const float a = fabsf(hp);
        f->env = a > f->env ? att * f->env + (1 - att) * a : rel * f->env + (1 - rel) * a;
        const float over = LIN2DB(f->env) - thr;
        const float target = over > 0 ? fminf(over * 0.75f, maxRed) : 0.0f; /* ~4:1 */
        f->red = smooth * f->red + (1 - smooth) * target;
        /* współczynniki przeliczane co 16 próbek, gdy ściszenie się zmieniło */
        if ((f->counter++ & 15) == 0 && fabsf(f->red - f->lastRed) > 0.05f) {
            shelf(f, fc * 0.8f, -f->red);
            f->lastRed = f->red;
        }
        const float y = f->sb0 * x + f->sb1 * f->sx1 + f->sb2 * f->sx2 - f->sa1 * f->sy1 - f->sa2 * f->sy2;
        f->sx2 = f->sx1;
        f->sx1 = x;
        f->sy2 = f->sy1;
        f->sy1 = y;
        f->port[1][i] = y;
    }
}

/* ---- limiter: 0 in, 1 out, 2 ceiling dB, 3 release ms ---- */
static void run_limit(LADSPA_Handle h, unsigned long n)
{
    Fx *f = h;
    const float ceil = DB2LIN(*f->port[2]);
    const float rel = coef(*f->port[3], f->rate);
    for (unsigned long i = 0; i < n; i++) {
        const float x = f->port[0][i], a = fabsf(x);
        const float need = a > ceil ? ceil / a : 1.0f;
        f->gain = need < f->gain ? need : rel * f->gain + (1 - rel) * need; /* atak natychmiastowy */
        float y = x * f->gain;
        if (y > ceil) y = ceil; /* zabezpieczenie */
        if (y < -ceil) y = -ceil;
        f->port[1][i] = y;
    }
}

/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    LADSPA_Data lo, hi, def;
} Ctl;

static LADSPA_Descriptor *make(unsigned long id, const char *label, const char *name,
                               void (*run)(LADSPA_Handle, unsigned long), const Ctl *ctl, int nctl)
{
    LADSPA_Descriptor *d = calloc(1, sizeof *d);
    const int np = 2 + nctl;
    LADSPA_PortDescriptor *pd = calloc(np, sizeof *pd);
    const char **pn = calloc(np, sizeof *pn);
    LADSPA_PortRangeHint *ph = calloc(np, sizeof *ph);
    pd[0] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    pn[0] = "In";
    pd[1] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    pn[1] = "Out";
    for (int i = 0; i < nctl; i++) {
        pd[2 + i] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        pn[2 + i] = ctl[i].name;
        ph[2 + i].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE;
        ph[2 + i].LowerBound = ctl[i].lo;
        ph[2 + i].UpperBound = ctl[i].hi;
    }
    d->UniqueID = id;
    d->Label = label;
    d->Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    d->Name = name;
    d->Maker = "LogiHub";
    d->Copyright = "MIT";
    d->PortCount = np;
    d->PortDescriptors = pd;
    d->PortNames = pn;
    d->PortRangeHints = ph;
    d->instantiate = instantiate;
    d->connect_port = connect_port;
    d->activate = activate;
    d->run = run;
    d->cleanup = cleanup;
    return d;
}

static LADSPA_Descriptor *g_desc[4];

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    if (!g_desc[0]) {
        static const Ctl gate[] = {{"Open (dB)", -80, 0, -45}, {"Close (dB)", -90, 0, -55}, {"Release (ms)", 5, 1000, 120}};
        static const Ctl comp[] = {{"Threshold (dB)", -60, 0, -20}, {"Ratio", 1, 20, 3}, {"Attack (ms)", 0.1f, 200, 8},
                                   {"Release (ms)", 5, 2000, 150}, {"Makeup (dB)", -12, 24, 0}};
        static const Ctl deess[] = {{"Frequency (Hz)", 2000, 12000, 5500}, {"Threshold (dB)", -60, 0, -30},
                                    {"Max reduction (dB)", 0, 24, 6}};
        static const Ctl lim[] = {{"Ceiling (dB)", -20, 0, -1}, {"Release (ms)", 1, 1000, 60}};
        g_desc[0] = make(0x4C480001, "logihub_gate", "LogiHub Noise Gate", run_gate, gate, 3);
        g_desc[1] = make(0x4C480002, "logihub_compressor", "LogiHub Compressor", run_comp, comp, 5);
        g_desc[2] = make(0x4C480003, "logihub_deesser", "LogiHub De-esser", run_deess, deess, 3);
        g_desc[3] = make(0x4C480004, "logihub_limiter", "LogiHub Limiter", run_limit, lim, 2);
    }
    return index < 4 ? g_desc[index] : NULL;
}
