#pragma once

struct player;
struct input;

void open_window();
void draw(const char *str, struct player *player);
void close_window();

void client_handle_input(struct input *input);
