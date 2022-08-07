#include "draw.h"
#include "update.h"
#include <string.h>
#include <raylib.h>

#define WIDTH 800
#define HEIGHT 600

void open_window() {
    InitWindow(WIDTH, HEIGHT, "wow");
}

void draw(const char *str, struct player *player) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    DrawText(str, 10, 10, 20, BLACK);
    DrawCircle(player->x, player->y, 10.0f, RED);
    EndDrawing();
}

void close_window() {
    CloseWindow();
}

void client_handle_input(struct input *input) {
    memset(input->active, INPUT_NULL, sizeof(input->active));
    if (IsKeyDown(KEY_W))
        input->active[INPUT_MOVE_UP] = true;
    if (IsKeyDown(KEY_A))
        input->active[INPUT_MOVE_LEFT] = true;
    if (IsKeyDown(KEY_S))
        input->active[INPUT_MOVE_DOWN] = true;
    if (IsKeyDown(KEY_D))
        input->active[INPUT_MOVE_RIGHT] = true;
    if (IsKeyDown(KEY_Q))
        input->active[INPUT_QUIT] = true;
}
