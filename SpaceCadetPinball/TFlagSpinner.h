#pragma once
#include "TCollisionComponent.h"

class TFlagSpinner :
	public TCollisionComponent
{
public:
	TFlagSpinner(TPinballTable* table, int groupIndex);
	~TFlagSpinner() override;
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
		TEdgeSegment* edge) override;
	void NextFrame();
	void RefreshVisualPhase();
	void PaintHd(const rectangle_type& dirty) const;
	int VisualPhase() const { return visualPhase; }

	static void SpinTimer(int timerId, void* caller);

	float Speed{};
	float MaxSpeed;
	float MinSpeed;
	float SpeedDecrement;
	int SpinDirection{};
	int BmpIndex{};
	TEdgeSegment* PrevCollider;
private:
	struct Turn { int timer, due; };
	std::vector<Turn> pending;
	int visualStart{}, visualPhase{};
	int restartPhase{-1}; // Starting pose if the last paddle settled beyond its DAT switch position.
	void CancelTurns();
};

