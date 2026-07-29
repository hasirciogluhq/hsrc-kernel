#pragma once

#include <user/sdk/gfx.hpp>

struct ImDrawData;

bool ImGui_ImplUgx_Init();
void ImGui_ImplUgx_Shutdown();

/*
 * Feed MKDX input into ImGui for one frame.
 * client_top: OS chrome height (kChromeTitleH); ImGui coords are client-local.
 * prev_buttons: previous frame buttons (for drag-outside-window).
 */
void ImGui_ImplUgx_NewFrame(hsrc::sdk::Window &win, const hsrc::sdk::Input &in,
                            const hsrc::sdk::WindowOptions &opts,
                            uint8_t prev_buttons, int client_top);

/* Rasterize into the window surface. client_top shifts draw Y below chrome. */
void ImGui_ImplUgx_RenderDrawData(ImDrawData *draw_data, hsrc::sdk::Surface &surf,
                                  int client_top);
