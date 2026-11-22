#pragma once
#include <string>

// The caller creates the result directory and supplies the production UI draw.
bool VerifyGame(const std::string& directory, bool mixOpened, void (*drawUi)());
