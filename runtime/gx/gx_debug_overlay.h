// Optional, dependency-free GX inspection overlay. F10 toggles; F8 exports;
// arrows/page keys select a draw; F9 freezes the INSPECTOR snapshot only.
// This does not stop guest execution, nor does it provide full save states.
#pragma once
#include "render.h"
#include "render_gl.h"
#include "gl.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gx_debug {
struct Line { std::string text; };
static bool visible = false, frozen = false;
// F6 cycles off -> hide -> solo -> blink; no game state or GPU resources are rewritten.
static int isolation_mode = 0;
static const char* isolation_name() {
    static const char* names[] = {"OFF", "HIDE", "SOLO", "BLINK"};
    return names[isolation_mode];
}
static int selected = 0;
static int probe_map = 0;
static bool probe_enabled = false;
static uint64_t frame_no = 0;
static std::vector<gx::Cmd> cmds;
static std::vector<gx::PixelState> states;
static std::vector<size_t> draws;
static void update_filter() {
    // F9 is a *snapshot* freeze, not a guest pause. Stale draw IDs must never
    // select unrelated commands in later frames.
    const int command = visible && !frozen && !draws.empty() &&
        selected >= 0 && selected < (int)draws.size() ? (int)draws[selected] : -1;
    gx::render_debug_draw_filter(command, command >= 0 ? isolation_mode : 0);
    gx::render_debug_texture_probe(command, probe_map, probe_enabled && command >= 0);
}

static GLuint program = 0, vao = 0, vbo = 0;
static GLint u_unused = -1;

static bool handle_event(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN || e.key.repeat) return false;
    const SDL_Scancode k = e.key.keysym.scancode;
    if (k == SDL_SCANCODE_F10) { visible = !visible; update_filter(); return true; }
    if (!visible) return false;
    if (k == SDL_SCANCODE_F9) { frozen = !frozen; update_filter(); return true; }
    if (k == SDL_SCANCODE_F7) {
        gx::render_debug_request_frame_capture();
        fprintf(stderr, "[GX DEBUG] F7 requested paired framebuffer PNG capture\n");
        return true;
    }
    if (k == SDL_SCANCODE_F5) {
        probe_map = (probe_map + 1) % 8;
        update_filter();
        fprintf(stderr, "[GX DEBUG] texture probe map=%d active=%d\n", probe_map, (int)probe_enabled);
        return true;
    }
    if (k == SDL_SCANCODE_F4) {
        probe_enabled = !probe_enabled;
        update_filter();
        fprintf(stderr, "[GX DEBUG] texture probe %s, map=%d\n", probe_enabled ? "ON" : "OFF", probe_map);
        return true;
    }
    if (k == SDL_SCANCODE_F6) {
        isolation_mode = (isolation_mode + 1) % 4;
        update_filter();
        fprintf(stderr, "[GX DEBUG] draw isolation %s\n", isolation_name());
        return true;
    }
    if (k == SDL_SCANCODE_UP) { selected = std::max(0, selected - 1); update_filter(); return true; }
    if (k == SDL_SCANCODE_DOWN) { selected = std::min((int)draws.size()-1, selected + 1); selected = std::max(0, selected); update_filter(); return true; }
    if (k == SDL_SCANCODE_PAGEUP) { selected = std::max(0, selected - 10); update_filter(); return true; }
    if (k == SDL_SCANCODE_PAGEDOWN) { selected = std::min((int)draws.size()-1, selected + 10); selected = std::max(0, selected); update_filter(); return true; }
    if (k == SDL_SCANCODE_F8) {
        FILE* f = fopen("saves/gx-debug-snapshot.txt", "w");
        if (f) {
            fprintf(f, "GX debugger snapshot frame=%llu, draws=%zu, commands=%zu\n", (unsigned long long)frame_no, draws.size(), cmds.size());
            fprintf(f, "selected_draw=%d isolation=%s frozen=%d (filter disabled when frozen)\n",
                    selected, isolation_name(), frozen ? 1 : 0);
            fprintf(f, "texture_probe enabled=%d map=%d selected_draw=%d\n", probe_enabled ? 1 : 0, probe_map, selected);
            const auto gs = gx::render_debug_draw_stats();
            fprintf(f, "gpu_stats render_frame=%u command=%d mode=%d total=%u matched=%u filtered=%u reached=%u gl_submitted=%u async_skipped=%u\n",
                    gs.render_frame, gs.selected_command, gs.mode, gs.total_draws,
                    gs.selected_matches, gs.filtered_draws, gs.reached_draw_cmd,
                    gs.gl_submissions, gs.async_skips);
            fprintf(f, "present_stats efb_copies=%u display_copies=%u color_clears=%u depth_clears=%u present_cmds=%u present_calls=%u xfb_partial=%u xfb_completed=%u output_blits=%u output_clears=%u\n",
                    gs.efb_copies, gs.display_copies, gs.efb_color_clears, gs.efb_depth_clears,
                    gs.present_commands, gs.present_calls, gs.xfb_partial, gs.xfb_completed,
                    gs.output_blits, gs.output_clears);
            fprintf(f, "window_stats swaps=%u last_swap_render_frame=%u render_frame=%u present_before=%u present_after=%u efb_fbo=%u output_fbo=%u\n",
                    gs.host_swaps, gs.last_swap_render_frame, gs.render_frame,
                    gs.present_count_before, gs.present_count_after, gs.efb_fbo, gs.output_fbo);

            for (size_t i = 0; i < draws.size(); i++) {
                const auto& c = cmds[draws[i]];
                if (c.state >= states.size()) continue;
                const auto& st = states[c.state];
                fprintf(f, "draw=%zu cmd=%zu vertices=%u indices=%u prim=%u stages=%u tag=%u blend=%06X z=%06X alpha=%06X\n",
                        i, draws[i], c.vcount, c.count, c.prim, 1 + ((st.bp[0] >> 10) & 15), st.tag,
                        st.bp[0x41], st.bp[0x40], st.bp[0xF3]);
                for (int t=0; t<8; t++) if (st.tex_id[t])
                    fprintf(f,"  map=%d tex=%u efb=%u fmt=%u img0=%06X mode0=%06X\n",t,st.tex_id[t],st.tex_is_efb[t],st.tex_efb_fmt[t],st.bp[0x88+t],st.bp[0x80+t]);
                for (int t=0; t<1+int((st.bp[0]>>10)&15); t++)
                    fprintf(f,"  stage=%d cenv=%06X aenv=%06X order=%06X\n",t,st.bp[0xC0+2*t],st.bp[0xC1+2*t],st.bp[0x28+t/2]);
            }
            fclose(f); fprintf(stderr,"[GX DEBUG] wrote saves/gx-debug-snapshot.txt\n");
        } else fprintf(stderr,"[GX DEBUG] could not write snapshot\n");
        return true;
    }
    return false;
}
static void capture(const gx::Batch& b) {
    if (!visible || frozen) return;
    frame_no = gx::present_count();
    cmds = b.cmds;
    states = b.states;
    draws.clear();
    for (size_t i=0;i<cmds.size();i++) if (cmds[i].type==gx::CmdType::Draw) draws.push_back(i);
    if (selected >= (int)draws.size()) selected = std::max(0,(int)draws.size()-1);
    update_filter();
}
// Minimal 5x7 glyphs. Each row uses its low five bits, most significant pixel left.
static const uint8_t* glyph(char ch) {
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    struct G { char c; uint8_t p[7]; };
    static const G fonts[] = {
        {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
        {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
        {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
        {'G',{14,17,16,23,17,17,14}},{'H',{17,17,17,31,17,17,17}},
        {'I',{31,4,4,4,4,4,31}},{'J',{7,2,2,2,18,18,12}},
        {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
        {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
        {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
        {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
        {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
        {'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
        {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
        {'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},
        {'4',{2,6,10,18,31,2,2}},{'5',{31,16,30,1,1,17,14}},
        {'6',{6,8,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
        {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,2,12}},
        {' ',{0,0,0,0,0,0,0}},{':',{0,4,4,0,4,4,0}},
        {'=',{0,0,31,0,31,0,0}},{'.',{0,0,0,0,0,12,12}},
        {'/',{1,1,2,4,8,16,16}},{'-',{0,0,0,31,0,0,0}},
        {'[',{14,8,8,8,8,8,14}},{']',{14,2,2,2,2,2,14}},
        {'>',{16,8,4,2,4,8,16}},{'<',{1,2,4,8,4,2,1}},
        {'(',{2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}},
        {'#',{10,31,10,10,31,10,0}},{',',{0,0,0,0,4,4,8}},
        {'%',{17,2,4,4,8,17,0}},{'?',{14,17,1,2,4,0,4}},
        {'_',{0,0,0,0,0,0,31}},{'+',{0,4,4,31,4,4,0}},
    };
    if (ch>='a' && ch<='z') ch -= 32;
    for (const auto& f : fonts) if (f.c==ch) return f.p;
    return blank;
}
struct V { float x,y,r,g,b; };
static std::vector<V> vertices;
static int width=1,height=1;
static void rect(float x,float y,float w,float h,float r,float g,float b) {
    if (w<=0||h<=0) return;
    auto v=[&](float xx,float yy)->V{return {2*xx/width-1,1-2*yy/height,r,g,b};};
    auto a=v(x,y),bb=v(x+w,y),c=v(x+w,y+h),d=v(x,y+h);
    vertices.insert(vertices.end(),{a,bb,c,a,c,d});
}
static void label(float x,float y,const std::string& str,float r=0.88f,float g=0.91f,float b=0.98f) {
    for(char ch:str) {
        const uint8_t* p=glyph(ch);
        for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)
            if (p[yy] & (1<<(4-xx))) rect(x+xx*2,y+yy*2,2,2,r,g,b);
        x+=12;
        if(x>width-30)break;
    }
}
static GLuint shader(GLenum type,const char* src) {
    GLuint s=glCreateShader(type); glShaderSource(s,1,&src,nullptr); glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);fprintf(stderr,"[GX DEBUG] shader: %s\n",log);glDeleteShader(s);return 0;}
    return s;
}
static bool init() {
    if (program) return true;
    const char* vs="#version 330 core\nlayout(location=0) in vec2 pos;layout(location=1) in vec3 col;out vec3 rgb;void main(){rgb=col;gl_Position=vec4(pos,0,1);}";
    const char* fs="#version 330 core\nin vec3 rgb;out vec4 color;void main(){color=vec4(rgb,1);}";
    GLuint a=shader(GL_VERTEX_SHADER,vs), b=shader(GL_FRAGMENT_SHADER,fs);
    if(!a||!b){if(a)glDeleteShader(a);if(b)glDeleteShader(b);return false;}
    program=glCreateProgram();glAttachShader(program,a);glAttachShader(program,b);glLinkProgram(program);
    glDeleteShader(a);glDeleteShader(b);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){glDeleteProgram(program);program=0;return false;}
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),(void*)0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(V),(void*)(sizeof(float)*2));
    return true;
}
static void draw(int w,int h) {
    if(!visible||w<300||h<200)return;
    width=w;height=h;
    GLint old_program=0,old_vao=0,old_array=0,old_fbo=0,old_view[4];
    glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old_array);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_fbo);
    glGetIntegerv(GL_VIEWPORT,old_view);
    GLboolean old_mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK, old_mask);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST),stencil=glIsEnabled(GL_STENCIL_TEST);
    if(!init())return;
    vertices.clear();
    const float panel_w=std::min((float)w-16,760.f), panel_h=std::min((float)h-16,466.f);
    rect(8,8,panel_w,panel_h,.055f,.072f,.115f);
    char buf[256];
    snprintf(buf,sizeof(buf),"GX DEBUG - F10 HIDE - F9 %s - F8 EXPORT",frozen?"UNFREEZE":"FREEZE VIEW");
    label(18,20,buf,.45f,.85f,.98f);
    snprintf(buf,sizeof(buf),"FRAME %llu  DRAWS %zu  COMMANDS %zu  STATES %zu",(unsigned long long)frame_no,draws.size(),cmds.size(),states.size());
    label(18,43,buf);
    label(18,66,"UP/DOWN SELECT  PGUP/PGDN SKIP 10  F6 FILTER");
    snprintf(buf,sizeof(buf),"ISOLATION %s  %s", isolation_name(),
             frozen ? "DISABLED WHILE VIEW FROZEN" : "LIVE DRAW SELECTION");
    label(18,82,buf,.98f,.66f,.48f);
    if(!draws.empty()) {
        const auto &c=cmds[draws[selected]];
        if(c.state<states.size()) {
            const auto &st=states[c.state];
            snprintf(buf,sizeof(buf),"DRAW %d / %zu   COMMAND %zu   STATE %u",selected+1,draws.size(),draws[selected],c.state);
            label(18,94,buf,1.f,.84f,.42f);
            snprintf(buf,sizeof(buf),"VERTICES %u  INDICES %u  PRIM %u  TAG %u",c.vcount,c.count,c.prim,st.tag);label(18,116,buf);
            unsigned stages=1+((st.bp[0]>>10)&15);
            snprintf(buf,sizeof(buf),"TEV STAGES %u  ALPHA %06X  BLEND %06X  Z %06X",stages,st.bp[0xF3],st.bp[0x41],st.bp[0x40]);label(18,138,buf);
            int y=165;
            for(unsigned i=0;i<stages&&i<16&&y+17<panel_h-120;i++,y+=17){
                const uint32_t ord=st.bp[0x28+i/2];
                const uint32_t order=(ord>>(12*(i%2)))&0xFFF;
                snprintf(buf,sizeof(buf),"STAGE %02u MAP %u COORD %u CENV %06X AENV %06X",i,order&7,(order>>3)&7,st.bp[0xC0+2*i],st.bp[0xC1+2*i]);label(18,(float)y,buf);
            }
            y=std::max(y+10,285);
            for(int i=0;i<8&&y+17<panel_h-10;i++)if(st.tex_id[i]) {
                snprintf(buf,sizeof(buf),"TEX MAP %d ID %u EFB %u FMT %u",i,st.tex_id[i],st.tex_is_efb[i],st.tex_efb_fmt[i]);label(18,(float)y,buf,.68f,1.f,.73f);y+=18;
            }
        }
    } else label(18,94,"NO DRAWS IN CAPTURE");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,w,h);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
    glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(V),vertices.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,(GLsizei)vertices.size());
    glBindBuffer(GL_ARRAY_BUFFER,old_array);glBindVertexArray(old_vao);glUseProgram(old_program);
    glColorMask(old_mask[0],old_mask[1],old_mask[2],old_mask[3]);
    glBindFramebuffer(GL_FRAMEBUFFER,old_fbo);glViewport(old_view[0],old_view[1],old_view[2],old_view[3]);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    if(stencil)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);
}
} // namespace gx_debug
