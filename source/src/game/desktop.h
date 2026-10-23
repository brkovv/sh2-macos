#pragma once
/* Build-generated desktop adapters call these; original generated C is immutable. */
extern unsigned ee_desktop_fps;
void sub_00447830_desktop_original(void);
void sub_0055BF60_desktop_original(void);
void sub_0055C6A0_desktop_original(void);
void sub_0055C720_desktop_original(void);
void sub_0055CC60_desktop_original(void);
int ds_music_start(unsigned owner, float *pcm, unsigned frames, unsigned loop_start, unsigned loop_end);
int ds_music_stop(unsigned owner);
int ds_music_status(unsigned owner);
int ds_music_volume(unsigned owner, int volume);
int ee60_map_marker(void);
int ee60_spray_active(void);
void ee60_init(void);
