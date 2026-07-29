#pragma once

#include <user/sdk/wm.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/kilim.hpp>

struct ImDrawData;

bool ImGui_ImplKilim_Init(reed::Device *dev);
void ImGui_ImplKilim_Shutdown();

void ImGui_ImplKilim_NewFrame(wm::Window &win, const wm::Input &in,
                              const wm::WindowOptions &opts, uint8_t prev_buttons,
                              int client_top);

/* Emit ImGui draw lists as Kilim/Reed GPU cmds (no CPU FB writes). */
void ImGui_ImplKilim_RenderDrawData(ImDrawData *draw_data, kilim::Context &k,
                                    int client_top);
