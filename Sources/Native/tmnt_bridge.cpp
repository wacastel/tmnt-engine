// Native host bridge. The pinned board driver is included unchanged so its
// private input/RAM state stays encapsulated in this translation unit.
#include "tmnt_bridge.h"
#include "tmnt_media_validate.h"
#include "../Hardware/burn/drv/konami/d_tmnt.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

struct Session {
    bool active=false, invincible=false;
    int player=1;
    uint64_t frame=0;
    std::string assets, saves, error;
    uint32_t state[128]{};
    uint32_t boardPixels[304*224]{};
    uint8_t pixels[304*224*4]{};
    int16_t audio[735*2]{};
} session;
static bool libraryReady=false;
static bool valid(void *handle) { return handle == &session && session.active; }
extern "C" int tmnt_player_invincible_for(unsigned base) {
    return session.active && session.invincible &&
        base == 0x62000u + (session.player - 1) * 0x50u;
}
extern "C" int tmnt_native_cpu_index() { return SekGetActive(); }
extern "C" int tmnt_native_failed() { return !session.error.empty(); }
extern "C" void tmnt_native_fault(const char *reason, unsigned pc, unsigned detail) {
    char text[192];
    std::snprintf(text,sizeof(text),"%s: unsupported or changed fixed instruction at %06X (%04X)",reason,pc,detail);
    if (session.error.empty()) session.error=text;
}
static UINT32 color(INT32 r, INT32 g, INT32 b, INT32) {
    return 0xff000000u | (b << 16) | (g << 8) | r;
}
static INT32 load(UINT8 *destination, INT32 *written, INT32 index) {
    BurnRomInfo identity{}; char *name=nullptr;
    if (BurnDrvGetRomInfo(&identity,index) || BurnDrvGetRomName(&name,index,0) || !name) return 1;
    std::string path=session.assets+"/"+name;
    FILE *file=std::fopen(path.c_str(),"rb"); if (!file) return 1;
    size_t count=std::fread(destination,1,identity.nLen,file);
    bool failed=std::ferror(file); std::fclose(file);
    if (written) *written=(INT32)count;
    return failed || count != identity.nLen;
}
static void clearInputs() {
    UINT8 *ports[]={DrvInputPort0,DrvInputPort1,DrvInputPort2,DrvInputPort3,DrvInputPort4,DrvInputPort5};
    for (auto port:ports) std::memset(port,0,8);
}
static unsigned byte(unsigned offset) { return Drv68KRam[offset ^ 1]; }
static unsigned word(unsigned offset) { return (byte(offset)<<8)|byte(offset+1); }
static void snapshot() {
    uint32_t *s=session.state; std::memset(s,0,sizeof(session.state));
    s[0]=1; s[1]=(uint32_t)session.frame; s[3]=session.player;
    SekOpen(0); s[2]=SekGetPC(-1);
    for (int i=0;i<16;i++) s[16+i]=SekGetDAR(i);
    s[32]=m68k_get_reg(nullptr,M68K_REG_SR); s[33]=SekTotalCycles(); SekClose();
    ZetOpen(0); s[4]=ZetGetPC(-1); s[56]=ZetBc(-1); s[57]=ZetDe(-1); s[58]=ZetHL(-1); s[59]=ZetSP(-1); s[60]=ZetTotalCycles();
    Z80_Regs z{}; Z80GetContext(&z);
    const Z80_PAIR pairs[]={z.prvpc,z.pc,z.sp,z.af,z.bc,z.de,z.hl,z.ix,z.iy,z.af2,z.bc2,z.de2,z.hl2,z.wz};
    for (int i=0;i<14;i++) s[64+i]=pairs[i].d;
    const uint8_t zs[]={z.r,z.r2,z.iff1,z.iff2,z.halt,z.im,z.i,z.nmi_state,z.nmi_pending,z.irq_state,z.vector,z.after_ei,z.after_retn};
    for (int i=0;i<13;i++) s[78+i]=zs[i];
    s[91]=z.cycles_left; s[92]=z.ICount; s[93]=z.end_run; s[94]=z.EA; s[95]=z.hold_irq; ZetClose();
    unsigned p=0x2000+(session.player-1)*0x50;
    s[5]=byte(0x101+(session.player-1)*8); s[6]=byte(p+8); s[7]=byte(p);
    s[8]=DrvInput[session.player]; s[9]=bIrqEnable; s[10]=session.invincible;
    s[11]=DrvInput[0]; s[12]=word(p+8); s[13]=byte(p+0x2d);
    for (int i=0;i<4;i++) { unsigned o=0x2000+i*0x50; s[96+i*4]=byte(0x101+i*8); s[97+i*4]=word(o+8); s[98+i*4]=byte(o); s[99+i*4]=byte(o+0x2d); }
}
extern "C" void *tmnt_create(const char *assets,const char *saves) {
    if (session.active) return nullptr;
    session.error.clear(); char error[256];
    if (!tmnt_validate_media(assets,error,sizeof(error))) { session.error=error; return nullptr; }
    session.assets=assets; session.saves=saves?saves:""; session.frame=0; session.invincible=false; session.player=1;
    std::memset(session.boardPixels,0,sizeof(session.boardPixels));
    std::memset(session.pixels,0,sizeof(session.pixels)); std::memset(session.audio,0,sizeof(session.audio));
    if (!libraryReady) { BurnLibInit(); libraryReady=true; }
    nBurnDrvActive=0; nBurnSoundRate=44100; nBurnSoundLen=735;
    nBurnBpp=4; nBurnPitch=304*4; nCurrentFrame=0;
    pBurnDraw=reinterpret_cast<UINT8 *>(session.boardPixels); pBurnSoundOut=session.audio;
    BurnHighCol=color; BurnExtLoadRom=load;
    DrvDip[0]=0xff; DrvDip[1]=0x5e; DrvDip[2]=0xff;
    clearInputs(); std::memset(DrvInput,0,sizeof(DrvInput)); DrvReset=0;
    // Musashi clears its shared CPU context only once; start each new session cold.
    void *cold=std::calloc(1,m68k_context_size());
    if (!cold) { session.error="Could not allocate CPU context."; return nullptr; }
    m68k_set_context(cold); std::free(cold);
    if (BurnDrvInit()) { session.error="Could not initialize verified TMNT hardware."; return nullptr; }
    BurnRandomSetSeed(0x303808909313ULL);
    session.active=true; snapshot(); return &session;
}
extern "C" void tmnt_destroy(void *h) {
    if (valid(h)) { BurnDrvExit(); session.active=false; }
}
extern "C" int tmnt_reset(void *h) {
    if (!valid(h)) return 0;
    std::string a=session.assets,s=session.saves; int player=session.player; bool invincible=session.invincible;
    tmnt_destroy(h); if (!tmnt_create(a.c_str(),s.c_str())) return 0;
    session.player=player; session.invincible=invincible; snapshot(); return 1;
}
extern "C" const char *tmnt_error(void *) { return session.error.c_str(); }
extern "C" int tmnt_step(void *h,float x,float y,float unused,uint32_t buttons) {
    if (!valid(h) || !session.error.empty()) return 0;
    if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(unused)||x < -1||x > 1||y < -1||y > 1||unused!=0||(buttons & ~15u)) {
        session.error="Invalid host controls"; return 0;
    }
    clearInputs();
    UINT8 *ports[]={DrvInputPort1,DrvInputPort2,DrvInputPort3,DrvInputPort4};
    UINT8 *p=ports[session.player-1];
    DrvInputPort0[session.player-1]=!!(buttons & TMNT_COIN);
    p[0]=x < -0.25f; p[1]=x > 0.25f; p[2]=y > 0.25f; p[3]=y < -0.25f;
    // The supplied four-player cabinet has no Start switch; attack joins.
    p[4]=!!(buttons & (TMNT_ATTACK|TMNT_START)); p[5]=!!(buttons & TMNT_JUMP);
    std::memset(session.audio,0,sizeof(session.audio));
    BurnDrvFrame();
    // KonamiBlendCopy's 32-bit path emits packed 0x00RRGGBB directly, bypassing
    // BurnHighCol. Convert that board output to the C API's opaque RGBA8888.
    for (unsigned i=0;i<304u*224u;i++) {
        const uint32_t pixel=session.boardPixels[i];
        session.pixels[4*i+0]=(pixel>>16)&255;
        session.pixels[4*i+1]=(pixel>>8)&255;
        session.pixels[4*i+2]=pixel&255;
        session.pixels[4*i+3]=255;
    }
    session.frame++; nCurrentFrame++; snapshot();
    return session.error.empty();
}
extern "C" void tmnt_set_invincible(void *h,int enabled) {
    if (!valid(h)) return;
#ifdef TMNT_REFERENCE
    (void)enabled; session.invincible=false;
#else
    session.invincible=enabled!=0;
#endif
}
extern "C" int tmnt_get_invincible(void *h) { return valid(h) && session.invincible; }
extern "C" void tmnt_set_player(void *h,int player) {
    if (valid(h) && player>=1 && player<=4) { session.player=player; clearInputs(); snapshot(); }
}
extern "C" int tmnt_get_player(void *h) { return valid(h)?session.player:0; }
extern "C" const uint8_t *tmnt_pixels(void *h) { return valid(h)?session.pixels:nullptr; }
extern "C" const int16_t *tmnt_audio(void *h) { return valid(h)?session.audio:nullptr; }
extern "C" int tmnt_audio_count(void *h) { return valid(h)?735:0; }
extern "C" int tmnt_width(void *) { return 304; }
extern "C" int tmnt_height(void *) { return 224; }
extern "C" double tmnt_frame_rate(void *) { return 60.; }
extern "C" uint64_t tmnt_frame_number(void *h) { return valid(h)?session.frame:0; }
extern "C" const uint32_t *tmnt_state(void *h) { return valid(h)?session.state:nullptr; }
extern "C" int tmnt_state_size(void *) { return 512; }
extern "C" const uint8_t *tmnt_ram(void *h) { return valid(h)?RamStart:nullptr; }
extern "C" int tmnt_ram_size(void *h) { return valid(h)?(int)(RamEnd-RamStart):0; }
