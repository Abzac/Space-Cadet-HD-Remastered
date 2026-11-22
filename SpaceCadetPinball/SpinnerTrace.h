#pragma once

#include "TBall.h"
#include "TFlagSpinner.h"
#include "TLine.h"
#include "TPinballTable.h"
#include "pb.h"
#include "timer.h"

// This workload can also be built verbatim against HD 3 to compare the
// physical eight-position switches and scoring clocks independently of art.
namespace spinnerTrace
{
inline void tick()
{
	++pb::time_ticks;
	timer::check();
}

inline void freshTable()
{
	pb::MainTable->Message(MessageCode::Reset, 0);
	pb::replay_level(false);
	pb::replay_level(false);
	for (int t = 0; t < 1100; ++t) tick();
}

inline void cross(TFlagSpinner* flag, bool reverse, float speed)
{
	auto edge = static_cast<TLine*>(reverse ? flag->PrevCollider : flag->EdgeList.front());
	auto ball = pb::MainTable->BallList.front();
	vector2 point{(edge->X0 + edge->X1) * .5f, (edge->Y0 + edge->Y1) * .5f};
	auto normal = edge->Line.PerpendicularC;
	ball->Speed = speed;
	ball->Direction = {-normal.X, -normal.Y, 0};
	ball->EdgeCollisionCount = 0;
	flag->Collision(ball, &point, &normal, 0, edge);
}

inline bool run(const std::string& path)
{
	auto file = fopenu(path.c_str(),"wb");
	if (!file) return false;
	fputs("spinner,direction,contacts,tick,dat_phase,score_delta\n",file);
	for (int number : {1,2})
		for (bool reverse : {false,true})
			for (int contacts : {1,2})
			{
				freshTable();
				const auto table = pb::MainTable;
				auto p = static_cast<TFlagSpinner*>(table->find_component(("a_flag"+std::to_string(number)).c_str()));
				const int initial = table->CurScore;
				for (int t = 0; t < 1150; ++t)
				{
					if (t == 0 || (contacts == 2 && t == 37))
						cross(p, t == 0 ? reverse : !reverse, 10.f);
					fprintf(file,"%d,%d,%d,%d,%d,%d\n",number,reverse ? -1 : 1,contacts,t,
					        p->BmpIndex,table->CurScore-initial);
					tick();
				}
			}
	const bool valid = !ferror(file);
	return fclose(file) == 0 && valid;
}
}
