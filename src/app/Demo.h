// Demo.h — "--demo" quick start: generates a few WAV files in the cache dir and imports them so a
// first-time user (and the UI smoke test) sees real tiles, a waveform and OSC rows.
#pragma once

#include <string>
#include <vector>

namespace evobox
{

class Application;

namespace demo
{
// Creates beat/tone/sweep WAVs (once) and returns their paths.
std::vector<std::string> ensureDemoAudio();
// Imports the demo audio into the current category.
void importDemoAudio(Application& app);
// Seeds a small offline gift catalog (only when the cache is empty) so the gallery has tiles.
void seedDemoCatalog(Application& app);
// Called once per frame by the smoke test driver: trims the first imported sound after `frame` reaches
// `saveAtFrame`, adds OSC commands to it and selects it. Returns true when done.
bool driveSmokeDemo(Application& app, int frame, int saveAtFrame);
// Smoke variant for the Gifts tab: selects a gift, configures it and simulates it once.
bool driveSmokeGifts(Application& app, int frame, int atFrame);
} // namespace demo

} // namespace evobox
