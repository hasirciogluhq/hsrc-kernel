#pragma once

#include <user/sdk/wm.hpp>
#include <user/sdk/reed.hpp>

struct ImDrawData;

bool ImGui_ImplKilim_Init();
void ImGui_ImplKilim_Shutdown();

void ImGui_ImplKilim_NewFrame(wm::Window &win, const wm::Input &in,
                              const wm::WindowOptions &opts, uint8_t prev_buttons,
                              int client_top);

/* Software-rasterize into the RT color texture (already cleared by kilim). */
void ImGui_ImplKilim_RenderDrawData(ImDrawData *draw_data, reed::Texture2D &fb,
                                    int client_top);
