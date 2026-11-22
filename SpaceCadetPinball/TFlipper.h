#pragma once
#include "TCollisionComponent.h"

class TFlipperEdge;

class TFlipper :
	public TCollisionComponent
{
public:
	TFlipper(TPinballTable* table, int groupIndex);
	~TFlipper() override;
	int Message(MessageCode code, float value) override;
	void port_draw() override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
		TEdgeSegment* edge) override;
	void UpdateSprite();
	static void TrackDrainApproach(TBall& ball, const TPinballTable& table);
	int GetFlipperStepAngle(float dt, float* dst) const;
	void FlipperCollision(float deltaAngle);
	
	int BmpIndex;
	float FrameSweep{};
	TFlipperEdge* FlipperEdge;
private:
	bool TryTipSave(TBall& ball, float deltaAngle);
};
