#include "Combat/DBWeaponData.h"

#include "Animation/AnimMontage.h"

bool FDBAttackDefinition::IsValid() const
{
	if (Id == EDBAttackId::None || !Montage || !FMath::IsFinite(PlayRate) || PlayRate <= 0.0f
	    || !FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0.0f || !FMath::IsFinite(Damage)
	    || Damage <= 0.0f || !FMath::IsFinite(GroundKnockbackDistance) || GroundKnockbackDistance < 0.0f
	    || !FMath::IsFinite(WeaponTraceRadius) || WeaponTraceRadius < 0.0f
	    || (WeaponTraceRadius > 0.0f && WeaponTraceRadius < 0.5f) || HitWindows.IsEmpty()
	    || !FMath::IsFinite(ComboStart) || ComboStart >= Montage->GetPlayLength() || !FMath::IsFinite(EvadeCancelStart)
	    || EvadeCancelStart < 0.0f || EvadeCancelStart >= Montage->GetPlayLength() || !FMath::IsFinite(JumpCancelStart)
	    || JumpCancelStart >= Montage->GetPlayLength() || !FMath::IsFinite(MoveCancelStart)
	    || MoveCancelStart >= Montage->GetPlayLength())
	{
		return false;
	}
	if (!FMath::IsFinite(StaggerResistanceWindow.Start) || !FMath::IsFinite(StaggerResistanceWindow.End)
	    || StaggerResistanceWindow.Start < 0.0f || StaggerResistanceWindow.End < StaggerResistanceWindow.Start
	    || StaggerResistanceWindow.End > ComboStart)
	{
		return false;
	}
	float PreviousEnd = 0.0f;
	for (const FDBAttackWindow& Window : HitWindows)
	{
		if (!FMath::IsFinite(Window.Start) || !FMath::IsFinite(Window.End) || Window.Start < PreviousEnd
		    || Window.End <= Window.Start || Window.End > ComboStart || Window.End > JumpCancelStart
		    || Window.End > MoveCancelStart)
		{
			return false;
		}
		PreviousEnd = Window.End;
	}
	return true;
}
