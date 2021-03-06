/*
 *  Copyright 2016 Davide Pianca
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <drivers/video.h>
#include <lib/string.h>
#include <mm/memory.h>
#include <graphics.h>
#include <gui/window.h>
#include <gui/font.h>

static int x;
static int y;
struct video_mem vram;
struct vbe_mem vbemem;

void video_init(int h, int w) {
    x = 0;
    y = 0;
    vram.heigth = h;
    vram.width = w;
    vram.ram = (uint16_t *) 0xB8000;
}

void printk(char *buffer, ...) {
    char str[256];
    va_list args;
    
    va_start(args, buffer);
    vsprintf(str, buffer, args);
    va_end(args);
    
    printk_string(str);
}

void printk_string(char *buffer) {
    int i = 0;
    
    check();
    
    while(buffer[i] != '\0') {
        switch(buffer[i]) {
            case '\0':
                return;
            case '\b':
                vram.ram[(y * vram.width) + --x] = (uint16_t) 3872;
                i++;
                break;
            case '\n':
                i++;
                y++;
                x = 0;
                check();
                break;
            case '\r':
                i++;
                x = 0;
                break;
            default:
                check();
                vram.ram[(y * vram.width) + x++] = (uint16_t) (3840 | buffer[i++]);
                break;
        }
    }
}

void check() {
    if(x >= vram.width) {
        x = 0;
        y++;
    }
    if(y >= vram.heigth) {
        y--;
        scroll();
    }
}

void scroll() {
    for(int i = 0; i < vram.width * (vram.heigth - 1); i++) {
        vram.ram[i] = vram.ram[i + vram.width];
    }
    for(int i = 0; i < vram.width; i++) {
        vram.ram[vram.width * (vram.heigth - 1) + i] = 3872;
    }
}

void clear() {
    x = y = 0;
    for(int i = 0; i < vram.heigth * vram.width; i++) {
        *(vram.ram + i) = (uint16_t) 3872;
    }
}

void vbe_init(multiboot_info_t *info) {
    vbe_controller_info_t *vbe_contr = (vbe_controller_info_t *) info->vbe_control_info;
    vbe_mode_info_t *vbe_mode = (vbe_mode_info_t *) info->vbe_mode_info;
    
    vbemem.buffer_size = vbe_mode->width * vbe_mode->height * (vbe_mode->bpp / 8);
    vbemem.mem = (uint32_t *) vbe_mode->framebuffer;
    vbemem.xres = vbe_mode->width;
    vbemem.yres = vbe_mode->height;
    vbemem.bpp = vbe_mode->bpp;
    vbemem.pitch = vbe_mode->pitch;
    
    if(!vbe_mode->framebuffer || vbe_contr->version < 0x0200 || !(vbe_mode->attributes & 0x8)) {
        if(vbe_mode->width != 80 && vbe_mode->height != 25) {
            regs16_t regs;
            regs.ax = 0x3;
            int32(0x10, &regs);
        }
    } else {
        uint32_t addr = (uint32_t) vbe_mode->framebuffer;
        uint32_t addr_buf = addr + vbemem.buffer_size;
        vbemem.buffer = (uint32_t *) addr_buf;
        for(int i = 0; i < (int) vbemem.buffer_size / PAGE_SIZE; i++, addr += PAGE_SIZE, addr_buf += PAGE_SIZE) {
            vmm_map(get_kern_directory(), addr_buf, PAGE_PRESENT | PAGE_RW);
            vmm_map_phys(get_kern_directory(), addr, addr, PAGE_PRESENT | PAGE_RW);
        }
        windows_list_init();
    }
}

void refresh_screen() {
    for(;;) {
        paint_desktop();
        memcpy(vbemem.mem, vbemem.buffer, vbemem.buffer_size);
    }
}

void draw_pixel(int x, int y, uint32_t color) {
    if(x < 0 || x > vbemem.xres || y < 0 || y > vbemem.yres)
        return;
    x = x * (vbemem.bpp / 8);
    y = y * vbemem.pitch;
    
    register uint8_t *pixel = (uint8_t *) ((uint32_t) vbemem.buffer) + x + y;
    pixel[0] = color & 0xFF;
    pixel[1] = (color >> 8) & 0xFF;
    pixel[2] = (color >> 16) & 0xFF;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if(x < 0 || x > vbemem.xres || y < 0 || y > vbemem.yres)
        return;
    
    uint8_t r = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    
    uint8_t *where = (uint8_t *) (((uint32_t) vbemem.buffer) + (x * (vbemem.bpp / 8)) + (y * vbemem.pitch));
    uint32_t row = vbemem.xres * (vbemem.bpp / 8);
    
    for(int i = 0; i < h; i++) {
        for(int j = 0; j < w; j++) {
            where[j * 4] = r;
            where[j * 4 + 1] = g;
            where[j * 4 + 2] = b;
        }
        where += row;
    }
}

void draw_string(int x, int y, char *string) {
    int startx = x;
    while(*string) {
        switch(*string) {
            case '\0':
                return;
            case '\b':
                // TODO
                break;
            case '\r':
                // TODO
                break;
            case '\n':
                y += 9;
                x = startx;
                break;
            case ' ':
                x += 9;
                break;
            default:
                draw_char(x, y, fonts[(int) *string]);
                x += 9;
                break;
        }
        string++;
    }
}

void draw_char(int x, int y, char *font_char) {
    for(int i = 0; i < 8; i++) {
        int l = 0;
        for(int j = 7; j >= 0; j--) {
            if(font_char[i] & (1 << j)) {
                draw_pixel(x + l, y + i, 0x000000);
            }
            l++;
        }
    }
}

int is_text_mode() {
    if(vbemem.mem != 0) {
        return 0;
    } else {
        return 1;
    }
}
