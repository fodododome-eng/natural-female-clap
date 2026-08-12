#include <clap/clap.h>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>
#include <algorithm>

namespace {

constexpr uint32_t N = 1024;
constexpr uint32_t HOP = 512;
constexpr float PI = 3.14159265358979323846f;
constexpr float FORMANT = 1.18f;   // Natural Female starting point
constexpr int LIFTER = 36;         // cepstral envelope cutoff

struct Plugin {
    clap_plugin_t clap{};
    const clap_host_t* host{};

    float sample_rate{48000.0f};
    std::vector<float> in_hist;
    std::vector<float> out_ring;
    uint64_t in_pos{0};
    uint64_t out_pos{0};
    uint64_t next_frame{N};

    std::vector<float> window;
    std::vector<std::complex<float>> spec;
    std::vector<std::complex<float>> tmp;
    std::vector<float> logmag;
    std::vector<float> env;
    std::vector<float> env_shift;
    std::vector<float> frame;

    Plugin(const clap_host_t* h) : host(h),
        in_hist(8192, 0.0f), out_ring(8192, 0.0f),
        window(N), spec(N), tmp(N), logmag(N), env(N), env_shift(N), frame(N)
    {
        for (uint32_t i=0;i<N;i++)
            window[i] = 0.5f - 0.5f*std::cos(2.0f*PI*(float)i/(float)(N-1));
    }

    static void fft(std::vector<std::complex<float>>& a, bool inverse) {
        const size_t n = a.size();
        for (size_t i=1,j=0;i<n;i++) {
            size_t bit=n>>1;
            for (; j&bit; bit>>=1) j^=bit;
            j^=bit;
            if (i<j) std::swap(a[i],a[j]);
        }
        for (size_t len=2;len<=n;len<<=1) {
            float ang = 2.0f*PI/(float)len*(inverse ? 1.0f : -1.0f);
            std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for (size_t i=0;i<n;i+=len) {
                std::complex<float> w(1,0);
                for (size_t j=0;j<len/2;j++) {
                    auto u=a[i+j], v=a[i+j+len/2]*w;
                    a[i+j]=u+v;
                    a[i+j+len/2]=u-v;
                    w*=wlen;
                }
            }
        }
        if (inverse) {
            for (auto& x:a) x/=(float)n;
        }
    }

    float hist_at(uint64_t p) const {
        return in_hist[p % in_hist.size()];
    }

    float& out_at(uint64_t p) {
        return out_ring[p % out_ring.size()];
    }

    void process_frame(uint64_t center) {
        // Read the most recent N samples ending at center.
        uint64_t start = center - N;
        for (uint32_t i=0;i<N;i++)
            frame[i] = hist_at(start+i) * window[i];

        for (uint32_t i=0;i<N;i++)
            spec[i] = std::complex<float>(frame[i],0.0f);

        fft(spec,false);

        for (uint32_t k=0;k<N;k++)
            logmag[k] = std::log(std::max(std::abs(spec[k]), 1e-7f));

        // Real cepstrum: low quefrency contains a smooth spectral envelope.
        tmp.resize(N);
        for (uint32_t k=0;k<N;k++)
            tmp[k] = std::complex<float>(logmag[k],0.0f);
        fft(tmp,true);

        for (uint32_t q=LIFTER+1;q<N-LIFTER;q++)
            tmp[q]=0.0f;

        fft(tmp,false);

        for (uint32_t k=0;k<N;k++)
            env[k] = tmp[k].real();

        // Shift only the smooth envelope upward in frequency.
        for (uint32_t k=0;k<N;k++) {
            float src = (float)k / FORMANT;
            if (src >= (float)(N-1)) {
                env_shift[k] = env[N-1];
            } else {
                uint32_t i=(uint32_t)src;
                float f=src-i;
                env_shift[k] = env[i]*(1.0f-f) + env[std::min(i+1,N-1u)]*f;
            }
        }

        for (uint32_t k=0;k<N;k++) {
            float new_logmag = logmag[k] + (env_shift[k] - env[k]);
            float mag = std::exp(std::min(new_logmag, 8.0f));
            float phase = std::atan2(spec[k].imag(), spec[k].real());
            spec[k] = std::polar(mag, phase);
        }

        fft(spec,true);

        // OLA. The plugin intentionally leaves pitch untouched;
        // MicUp's built-in Pitch Shifter can be used before/after this plugin.
        uint64_t out_start = center - N;
        for (uint32_t i=0;i<N;i++)
            out_at(out_start+i) += spec[i].real() * window[i];

        // Hann + 50% overlap normalization.
        // Two Hann windows overlap to approximately 1.0.
    }

    bool init() {
        std::fill(in_hist.begin(), in_hist.end(), 0.0f);
        std::fill(out_ring.begin(), out_ring.end(), 0.0f);
        in_pos=0; out_pos=0; next_frame=N;
        return true;
    }

    void activate(double sr) {
        sample_rate=(float)sr;
        init();
    }

    void process(const clap_process_t* p) {
        if (!p || p->audio_inputs_count==0 || p->audio_outputs_count==0) return;
        auto* in = p->audio_inputs[0].data32[0];
        auto* out = p->audio_outputs[0].data32[0];
        if (!in || !out) return;

        for (uint32_t i=0;i<p->frames_count;i++) {
            in_hist[in_pos % in_hist.size()] = in[i];
            ++in_pos;

            if (in_pos >= next_frame) {
                process_frame(next_frame);
                next_frame += HOP;
            }

            // One-frame processing delay is expected.
            uint64_t read_pos = (in_pos > N) ? (in_pos - N) : 0;
            float y = out_at(read_pos);
            out_at(read_pos)=0.0f;
            out[i]=y * 1.3333333f;
        }
    }
};

static const clap_plugin_descriptor_t DESC = {
    CLAP_VERSION_INIT,
    "com.openai.naturalfemale.formant",
    "Natural Female Formant",
    "OpenAI",
    "https://github.com/free-audio/clap",
    nullptr,
    nullptr,
    "0.1.0",
    "Lightweight real-time formant shifter for MicUp.",
    (const char*[]){CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MONO, CLAP_PLUGIN_FEATURE_PHASE_VOCODER, nullptr}
};

static bool init(const clap_plugin_t*) { return true; }
static void destroy(const clap_plugin_t* p) { delete static_cast<Plugin*>(p->plugin_data); }
static bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t) {
    auto* x=static_cast<Plugin*>(p->plugin_data); x->activate(sr); return true;
}
static void deactivate(const clap_plugin_t*) {}
static bool start(const clap_plugin_t*) { return true; }
static void stop(const clap_plugin_t*) {}
static void reset(const clap_plugin_t* p) { static_cast<Plugin*>(p->plugin_data)->init(); }

static uint32_t in_count(const clap_plugin_t*, bool is_input) { return 1; }
static bool port_get(const clap_plugin_t*, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
    if (index!=0 || !info) return false;
    info->id=0;
    std::strncpy(info->name, is_input ? "Input" : "Output", sizeof(info->name));
    info->name[sizeof(info->name)-1]=0;
    info->channel_count=1;
    info->flags=CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type=CLAP_PORT_MONO;
    info->in_place_pair=CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t AUDIO_PORTS = {in_count, port_get};

static const void* ext(const clap_plugin_t* p, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS)==0) return &AUDIO_PORTS;
    return nullptr;
}
static clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
    static_cast<Plugin*>(p->plugin_data)->process(pr);
    return CLAP_PROCESS_CONTINUE;
}
static void main_thread(const clap_plugin_t*) {}

static clap_plugin_t* create(const clap_host_t* host) {
    auto* x=new Plugin(host);
    x->clap.desc=&DESC;
    x->clap.plugin_data=x;
    x->clap.init=init;
    x->clap.destroy=destroy;
    x->clap.activate=activate;
    x->clap.deactivate=deactivate;
    x->clap.start_processing=start;
    x->clap.stop_processing=stop;
    x->clap.reset=reset;
    x->clap.process=process;
    x->clap.get_extension=ext;
    x->clap.on_main_thread=main_thread;
    return &x->clap;
}

static uint32_t count(const clap_plugin_factory_t*) { return 1; }
static const clap_plugin_descriptor_t* desc(const clap_plugin_factory_t*, uint32_t i) { return i==0 ? &DESC : nullptr; }
static const clap_plugin_t* make(const clap_plugin_factory_t*, const clap_host_t* h, const char* id) {
    return std::strcmp(id,DESC.id)==0 ? create(h) : nullptr;
}
static const clap_plugin_factory_t FACTORY = {count,desc,make};

static bool entry_init(const char*) { return true; }
static void entry_deinit() {}
static const void* get_factory(const char* id) {
    return std::strcmp(id,CLAP_PLUGIN_FACTORY_ID)==0 ? &FACTORY : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entry_init,
    entry_deinit,
    get_factory
};
