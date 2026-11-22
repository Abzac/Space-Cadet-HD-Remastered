#include "pch.h"
#include "TTextBoxMessage.h"
#include "pb.h"
#include "options.h"

CabinetText::CabinetText(const char* resource)
{
	if (!resource) return;
	// A few upstream call sites select already-localized pointers at runtime.
	for (int i=0; i<static_cast<int>(Msg::Max); ++i)
	{
		auto id=static_cast<Msg>(i);
		if (translations::GetTranslation(id)==resource) { key=id; return; }
	}
	value=resource;
}

std::string CabinetText::Render() const
{
	if (key==Msg::Max) return value;
	std::string result=translations::GetTranslation(key);
	if (key==Msg::STRING123 && result.find("%s")!=std::string::npos)
	{
		const auto pauseKey=options::Options.Key[~GameBindings::TogglePause].Inputs[0];
		const std::string alternate=pauseKey.Type==InputTypes::Keyboard && pauseKey.Value!=SDLK_SPACE ?
			pauseKey.GetShortInputDescription()+" / " : "";
		result.replace(result.find("%s"),2,alternate);
		return result;
	}
	if (argument==Msg::Max && value.empty()) return result;
	auto at=result.find('%');
	if (at!=std::string::npos)
	{
		const auto length=result.compare(at,3,"%ld")==0 ? 3 : 2;
		result.replace(at,length,argument==Msg::Max ? value : translations::GetTranslation(argument));
	}
	return result;
}

TTextBoxMessage::TTextBoxMessage(const CabinetText& content, float time, bool lowPriority)
	: Content(content), Text(nullptr)
{
	NextMessage = nullptr;
	Time = time;
	EndTicks = pb::time_ticks + static_cast<int>(time * 1000.0f);
	LowPriority = lowPriority;
	Translate();
}

void TTextBoxMessage::Translate()
{
	auto localized=Content.Render();
	auto replacement=new char[localized.size()+1];
	memcpy(replacement,localized.c_str(),localized.size()+1);
	delete[] Text;
	Text=replacement;
}

TTextBoxMessage::~TTextBoxMessage()
{
	delete[] Text;
}

float TTextBoxMessage::TimeLeft() const
{
	return static_cast<float>(EndTicks - pb::time_ticks) * 0.001f;
}

void TTextBoxMessage::Refresh(float time)
{
	this->Time = time;
	this->EndTicks = pb::time_ticks + static_cast<int>(time * 1000.0f);
}
