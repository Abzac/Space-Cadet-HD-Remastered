#pragma once
#include "translations.h"

// Keep the resource key and its argument while the message is queued. A language
// change can then redraw it without generating a game event or restarting its timer.
struct CabinetText
{
	Msg key=Msg::Max, argument=Msg::Max;
	std::string value;
	CabinetText() = default;
	CabinetText(Msg id) : key(id) {}
	CabinetText(Msg id, long long number) : key(id), value(std::to_string(number)) {}
	CabinetText(Msg id, Msg name) : key(id), argument(name) {}
	CabinetText(const char* resource);
	std::string Render() const;
};

class TTextBoxMessage
{
public:
	CabinetText Content;
	TTextBoxMessage* NextMessage;
	char* Text;
	float Time;
	int EndTicks;
	bool LowPriority;

	TTextBoxMessage(const CabinetText& content, float time, bool lowPriority);
	~TTextBoxMessage();
	void Translate();
	float TimeLeft() const;
	void Refresh(float time);
};
