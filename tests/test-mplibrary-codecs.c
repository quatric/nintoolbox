// Regression coverage for the MPLibrary gap ports: GC/Wii board encode,
// SMP + MP10 boards, PTD container encode, MPMESS v4/v5/v6 encode,
// BNFMSA skeletal animation tracks and XB canonical encode.
// Reference: KillzXGaming/MPLibrary (see CREDITS.md).
// Synthetic fixtures only; every check is a struct-level or byte-exact
// roundtrip, no retail samples required.
#include "lib-mpboard.h"
#include "lib-ptd.h"
#include "lib-mpmess.h"
#include "lib-bnfm.h"
#include "lib-xb.h"
#undef malloc
#undef calloc
#undef strdup
#undef free
#undef realloc
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// dclib poisons the libc allocators (forces CALLOC etc.); use private
// helpers here so this test stays linkable without all of dclib.
static void *go_calloc(unsigned n, unsigned sz) {
    void *p = malloc(n * (size_t)sz);
    if (p) memset(p, 0, n * (size_t)sz);
    return p;
}
static char *go_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// Stub: only ScanPTD/ScanMPMESS reference OwnedEntryAdd and neither is
// exercised here (dead-stripped or stubbed at link).
bool OwnedEntryAdd(nintendo_sarc_entry_t *e, unsigned i, const char *n,
    const unsigned char *d, unsigned s) {
    (void)e; (void)i; (void)n; (void)d; (void)s; return 0;
}

static int failures = 0;
static void check(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); failures++; }
}

// --- GC/Wii board encode -----------------------------------------------
static void t_mpboard(void) {
    mp_board_t b;
    memset(&b, 0, sizeof b);
    b.num_spaces = 2;
    b.version = 6;
    b.spaces = go_calloc(2, sizeof(mp_space_node_t));
    b.spaces[0].pos[0] = 1; b.spaces[0].pos[1] = 2; b.spaces[0].pos[2] = 3;
    b.spaces[0].rot[1] = 90;
    b.spaces[0].scale[0] = b.spaces[0].scale[1] = b.spaces[0].scale[2] = 1;
    b.spaces[0].param1 = 10; b.spaces[0].param2 = 20; b.spaces[0].param3 = 30;
    b.spaces[0].type_id = 5; b.spaces[0].num_links = 1; b.spaces[0].links[0] = 1;
    b.spaces[1].pos[0] = 4; b.spaces[1].pos[1] = 5; b.spaces[1].pos[2] = 6;
    b.spaces[1].scale[0] = b.spaces[1].scale[1] = b.spaces[1].scale[2] = 1;
    b.spaces[1].param1 = 11; b.spaces[1].param2 = 21; b.spaces[1].param3 = 31;
    b.spaces[1].type_id = 6; b.spaces[1].num_links = 1; b.spaces[1].links[0] = 0;
    u8 *out = 0; unsigned osz = 0;
    check(CreateMPBoard(&out, &osz, &b) == 0, "mpboard create");
    check(IsMPBoard(out, osz), "mpboard is");
    mp_board_t b2;
    memset(&b2, 0, sizeof b2);
    check(ScanMPBoard(&b2, out, osz, 6) == 0, "mpboard scan");
    check(b2.num_spaces == 2 && b2.spaces[0].param3 == 30
        && b2.spaces[1].links[0] == 0, "mpboard fields");
    u8 *out2 = 0; unsigned osz2 = 0;
    CreateMPBoard(&out2, &osz2, &b2);
    check(osz2 == osz && !memcmp(out, out2, osz), "mpboard byte-exact");
    ResetMPBoard(&b2);
    free(b.spaces); free(out); free(out2);
    printf("mpboard OK\n");
}

// --- SMP + MP10 boards ---------------------------------------------------
static void t_smp(void) {
    const char *csv = "ID,Child0,Child1,Child2,Child3,Type,Attr1,Attr2\n"
        "A001,1,2,,,EMPTY,foo,bar\nA002,,,,,SHOP,,\n";
    check(IsSMPBoard((const u8 *)csv, strlen(csv)), "smp is");
    smp_board_t s;
    memset(&s, 0, sizeof s);
    check(ScanSMPBoard(&s, (const u8 *)csv, strlen(csv)) == 0, "smp scan");
    check(s.num_spaces == 2 && !strcmp(s.spaces[0].id, "A001")
        && s.spaces[0].num_links == 2 && !strcmp(s.spaces[0].type, "EMPTY"),
        "smp fields");
    u8 *out = 0; unsigned osz = 0;
    check(CreateSMPBoard(&out, &osz, &s) == 0, "smp create");
    smp_board_t s2;
    memset(&s2, 0, sizeof s2);
    check(ScanSMPBoard(&s2, out, osz) == 0, "smp rescan");
    check(s2.num_spaces == 2 && !strcmp(s2.spaces[1].type, "SHOP"), "smp fields2");
    ResetSMPBoard(&s); ResetSMPBoard(&s2); free(out);
    printf("smp OK\n");
}

static void t_mp10(void) {
    const char *xml = "<?xml version=\"1.0\"?><root><XmlFile>board</XmlFile>"
        "<Version>1.0</Version><MasuData><No>3</No><Area>1</Area>"
        "<NodeName>N1</NodeName><MasuName>START</MasuName><Param>7</Param>"
        "<Uncountble>0</Uncountble><OneWay>1</OneWay><JumpStart>0</JumpStart>"
        "<JumpEnd>0</JumpEnd><PunishNotReturn>0</PunishNotReturn>"
        "<NextNoList Size=\"1\"><NextNo Index=\"0\">4</NextNo></NextNoList>"
        "<PrevNoList Size=\"0\"></PrevNoList>"
        "<Position><X>1.5</X><Y>2.5</Y><Z>3.5</Z></Position>"
        "<Quaternion><X>0</X><Y>0</Y><Z>0</Z><W>1</W></Quaternion>"
        "</MasuData></root>";
    check(IsMP10Board((const u8 *)xml, strlen(xml)), "mp10 is");
    mp10_board_t m;
    memset(&m, 0, sizeof m);
    check(ScanMP10Board(&m, (const u8 *)xml, strlen(xml)) == 0, "mp10 scan");
    check(m.num_masu == 1 && m.masu[0].id == 3
        && !strcmp(m.masu[0].type, "START") && m.masu[0].next[0] == 4
        && m.masu[0].pos[0] == 1.5f, "mp10 fields");
    u8 *out = 0; unsigned osz = 0;
    check(CreateMP10Board(&out, &osz, &m) == 0, "mp10 create");
    mp10_board_t m2;
    memset(&m2, 0, sizeof m2);
    check(ScanMP10Board(&m2, out, osz) == 0, "mp10 rescan");
    check(m2.num_masu == 1 && m2.masu[0].id == 3
        && !strcmp(m2.masu[0].type, "START"), "mp10 fields2");
    ResetMP10Board(&m); ResetMP10Board(&m2); free(out);
    printf("mp10 OK\n");
}

// --- PTD container -------------------------------------------------------
static void t_ptd(void) {
    u8 ch1d[16], ch2d[16], unk[144];
    for (int i = 0; i < 16; i++) { ch1d[i] = (u8)(i * 3 + 1); ch2d[i] = (u8)(i * 5 + 2); }
    for (int i = 0; i < 144; i++) unk[i] = (u8)(i + 7);
    ptd_file_t f;
    memset(&f, 0, sizeof f);
    f.version = 1; f.unknown2 = 2; f.sample_rate = 32000; f.channel_count = 2;
    f.num_streams = 3;
    f.streams = go_calloc(3, sizeof(ptd_stream_t));
    ptd_stream_t *s0 = &f.streams[0];
    s0->sample_rate = 32000; s0->nibble_count = 32;
    s0->num_channels = 1; s0->channels[0].unknown = 5;
    for (int i = 0; i < 16; i++) s0->channels[0].coef[i] = 100 + i;
    s0->channels[0].data = ch1d; s0->channels[0].data_size = 16;
    ptd_stream_t *s1 = &f.streams[1];
    s1->flags = 0x01000000u | 0x02000000u; s1->sample_rate = 32000;
    s1->nibble_count = 32; s1->loop_start = 4; s1->num_channels = 2;
    for (int i = 0; i < 16; i++) {
        s1->channels[0].coef[i] = 100 + i; s1->channels[1].coef[i] = 200 + i;
    }
    s1->channels[0].unknown = 6; s1->channels[1].unknown = 7;
    s1->channels[0].data = ch1d; s1->channels[0].data_size = 16;
    s1->channels[1].data = ch2d; s1->channels[1].data_size = 16;
    ptd_stream_t *s2 = &f.streams[2];
    s2->flags = 23871488u; s2->sample_rate = 32000; s2->nibble_count = 32;
    s2->num_channels = 2;
    for (int i = 0; i < 16; i++) {
        s2->channels[0].coef[i] = 300 + i; s2->channels[1].coef[i] = 200 + i;
    }
    s2->channels[0].unknown = 8; s2->channels[1].unknown = 9;
    s2->channels[0].data = ch2d; s2->channels[0].data_size = 16;
    s2->channels[1].data = ch1d; s2->channels[1].data_size = 16;
    s2->unknown_data = unk; s2->unknown_size = 144;
    u8 *out = 0; unsigned osz = 0;
    check(CreatePTD(&out, &osz, &f) == 0, "ptd create");
    check(IsPTD(out, osz), "ptd is");
    ptd_file_t g;
    memset(&g, 0, sizeof g);
    check(ScanPTDFile(&g, out, osz) == 0, "ptd scan");
    check(g.num_streams == 3 && g.streams[1].num_channels == 2
        && g.streams[1].channels[1].coef[0] == 200
        && g.streams[2].unknown_size == 144
        && !memcmp(g.streams[2].unknown_data, unk, 144), "ptd fields");
    u8 *out2 = 0; unsigned osz2 = 0;
    check(CreatePTD(&out2, &osz2, &g) == 0, "ptd recreate");
    check(osz2 == osz && !memcmp(out, out2, osz), "ptd byte-exact");
    ResetPTDFile(&g); free(f.streams); free(out); free(out2);
    printf("ptd OK\n");
}

// --- MPMESS v4/v5/v6 ------------------------------------------------------
static void t_mpmess_ver(unsigned ver) {
    mpmess_archive_t a;
    memset(&a, 0, sizeof a);
    a.version = ver; a.num_files = 2;
    a.files = go_calloc(2, sizeof(mpmess_file_t));
    a.files[0].num_entries = 3;
    a.files[0].entries = go_calloc(3, sizeof(mpmess_entry_t));
    a.files[0].entries[0].text = go_strdup("Hello World!");
    a.files[0].entries[0].id = 0x100;
    a.files[0].entries[1].text = go_strdup("[Dialog:Toad_Normal]Hi![COLOR:(RED)Hey]");
    a.files[0].entries[1].id = 0x101;
    a.files[0].entries[2].text = go_strdup("[ICON:A] Press [Select] [Align_2]end");
    a.files[0].entries[2].id = 0x102;
    a.files[1].num_entries = 1;
    a.files[1].entries = go_calloc(1, sizeof(mpmess_entry_t));
    a.files[1].entries[0].text = go_strdup("Line1\nLine2: test-mail_x.y@z!?");
    a.files[1].entries[0].id = 0x200;
    char tag[32];
    snprintf(tag, sizeof tag, "mpmess v%u", ver);
    u8 *out = 0; unsigned osz = 0;
    check(CreateMPMESS(&out, &osz, &a) == 0, tag);
    check(out && IsMPMESS(out, osz), tag);
    mpmess_archive_t b;
    memset(&b, 0, sizeof b);
    check(ScanMPMESSArchive(&b, out, osz) == 0 && b.version == ver
        && b.num_files == 2 && b.files[0].entries
        && b.files[0].num_entries == 3
        && !strcmp(b.files[0].entries[0].text, "Hello World!")
        && !strcmp(b.files[0].entries[1].text, "[Dialog:Toad_Normal]Hi![COLOR:(RED)Hey]")
        && !strcmp(b.files[0].entries[2].text, "[ICON:A] Press [Select] [Align_2]end")
        && !strcmp(b.files[1].entries[0].text, "Line1\nLine2: test-mail_x.y@z!?"), tag);
    if (ver >= 6) check(b.files[0].entries[0].id == 0x100, tag);
    u8 *out2 = 0; unsigned osz2 = 0;
    check(CreateMPMESS(&out2, &osz2, &b) == 0, tag);
    check(out2 && osz2 == osz && !memcmp(out, out2, osz), tag);
    ResetMPMESSArchive(&b);
    for (unsigned i = 0; i < 2; i++) {
        for (unsigned k = 0; k < a.files[i].num_entries; k++)
            free(a.files[i].entries[k].text);
        free(a.files[i].entries);
    }
    free(a.files); free(out); free(out2);
    printf("mpmess v%u OK\n", ver);
}

// --- BNFMSA ---------------------------------------------------------------
// Minimal BNFMSA blob builder (BE): 2 bones, TX/TY/RW tracks.
static void put32(unsigned char **p, unsigned v) {
    (*p)[0] = v >> 24; (*p)[1] = v >> 16; (*p)[2] = v >> 8; (*p)[3] = v;
    *p += 4;
}
static void putf(unsigned char **p, float v) {
    unsigned u;
    memcpy(&u, &v, 4);
    put32(p, u);
}
static unsigned char *mk_bnfmsa(unsigned *out_len) {
    static unsigned char buf[1024];
    unsigned char *p = buf;
    unsigned hdr[] = {2,1,2,2,0,0,4,4,0,0,0};
    for (unsigned i = 0; i < 11; i++) put32(&p, hdr[i]);
    unsigned o_anim = 76, o_ba = 108, o_tx = 276, o_ty = 304, o_rw = 332;
    unsigned o_kx = 360, o_ky = 376, o_kw = 392;
    unsigned o_walk = 408, o_root = 413, o_child = 418;
    unsigned offs[] = {0, o_anim, o_ba, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < 8; i++) put32(&p, offs[i]);
    put32(&p, o_walk); put32(&p, 0); put32(&p, 0); put32(&p, 2);
    put32(&p, 1); put32(&p, 0); put32(&p, 30); put32(&p, 0);
    put32(&p, o_root); put32(&p, 2);
    for (unsigned i = 0; i < 9; i++) put32(&p, 0);
    put32(&p, o_tx);
    for (unsigned i = 0; i < 9; i++) put32(&p, 0xFFFFFFFFu);
    put32(&p, o_child); put32(&p, 0); put32(&p, 2);
    for (unsigned i = 0; i < 7; i++) put32(&p, 0);
    put32(&p, 2);
    put32(&p, 0xFFFFFFFFu); put32(&p, o_ty);
    for (unsigned i = 0; i < 7; i++) put32(&p, 0xFFFFFFFFu);
    put32(&p, o_rw);
    { // tracks: TX, TY, RW (Normal, 2 keys)
        unsigned kos[] = {o_kx, o_ky, o_kw};
        for (unsigned i = 0; i < 3; i++) {
            put32(&p, kos[i]); put32(&p, 30); put32(&p, 0); put32(&p, 2);
            put32(&p, 0); put32(&p, 0);
            *p++ = 0; *p++ = 0; *p++ = 1; *p++ = 0;
        }
    }
    put32(&p, 0); putf(&p, 1.0f); put32(&p, 30); putf(&p, 4.0f);
    put32(&p, 0); putf(&p, 0.0f); put32(&p, 30); putf(&p, 2.0f);
    put32(&p, 0); putf(&p, 1.0f); put32(&p, 15); putf(&p, 0.5f);
    memcpy(p, "Walk\0root\0child\0", 16); p += 16;
    *out_len = (unsigned)(p - buf);
    static unsigned char owned[1024];
    memcpy(owned, buf, *out_len);
    return owned;
}

static void t_bnfmsa(void) {
    unsigned alen = 0;
    unsigned char *anim = mk_bnfmsa(&alen);
    model_t m;
    memset(&m, 0, sizeof m);
    m.num_joints = 2;
    m.joints = go_calloc(2, sizeof(joint_t));
    snprintf(m.joints[0].name, 64, "root");
    snprintf(m.joints[1].name, 64, "child");
    int ch = AppendBNFMSAAnimation(&m, anim, alen);
    check(ch == 3 && m.num_animations == 1, "bnfmsa count");
    model_animation_t *a = m.animations;
    check(!strcmp(a->name, "Walk") && a->num_channels == 3, "bnfmsa header");
    check(a->channels[0].node_idx == 0
        && a->channels[0].path == MODEL_ANIM_TRANSLATION
        && a->channels[0].values[0] == 1.0f
        && a->channels[0].values[3] == 4.0f, "bnfmsa T");
    check(a->channels[1].node_idx == 1
        && a->channels[2].path == MODEL_ANIM_ROTATION
        && a->channels[2].components == 4
        && a->channels[2].values[7] == 0.5f, "bnfmsa R");
    for (unsigned i = 0; i < a->num_channels; i++) {
        free(a->channels[i].times); free(a->channels[i].values);
    }
    free(a->channels); free(m.joints);
    printf("bnfmsa OK\n");
}

// --- XB canonical encode ---------------------------------------------------
static void t_xb(void) {
    const char *xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<root lang=\"en\">\n"
        "  <item>hi</item>\n"
        "  <MasuName>START &amp; GO</MasuName>\n"
        "  <PrevNoList Size=\"0\" />\n"
        "</root>\n";
    u8 *e1 = 0; unsigned s1 = 0;
    check(EncodeXB(&e1, &s1, xml, strlen(xml), 0) == 0, "xb encode");
    check(e1 && IsXB(e1, s1), "xb is");
    char *d1 = 0; size_t l1 = 0;
    check(DecodeXB_String(&d1, &l1, e1, s1) == 0, "xb decode");
    u8 *e2 = 0; unsigned s2 = 0;
    check(EncodeXB(&e2, &s2, d1, l1, 0) == 0, "xb re-encode");
    check(s1 == s2 && !memcmp(e1, e2, s1), "xb byte-stable");
    char *d2 = 0; size_t l2 = 0;
    DecodeXB_String(&d2, &l2, e2, s2);
    check(!strcmp(d1, d2) && strstr(d1, "&amp;"), "xb text-stable");
    u8 *e3 = 0; unsigned s3 = 0;
    char *d3 = 0; size_t l3 = 0;
    check(EncodeXB(&e3, &s3, xml, strlen(xml), 1) == 0, "xb wide");
    check(DecodeXB_String(&d3, &l3, e3, s3) == 0 && !strcmp(d1, d3), "xb wide text");
    free(e1); free(e2); free(e3); free(d1); free(d2); free(d3);
    printf("xb OK\n");
}

int main(void) {
    t_mpboard();
    t_smp();
    t_mp10();
    t_ptd();
    t_mpmess_ver(4);
    t_mpmess_ver(5);
    t_mpmess_ver(6);
    t_bnfmsa();
    t_xb();
    if (!failures) printf("MPLIBRARY CODECS ALL OK\n");
    return failures ? 1 : 0;
}
