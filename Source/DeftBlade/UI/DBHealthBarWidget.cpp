#include "UI/DBHealthBarWidget.h"

#include "Combat/DBHealthComponent.h"
#include "Components/ProgressBar.h"
#include "GameFramework/PlayerController.h"

void UDBHealthBarWidget::BindHealth(UDBHealthComponent* Health)
{
	if (HealthSource.IsValid())
	{
		HealthSource->OnHealthChanged.RemoveAll(this);
	}
	HealthSource = Health;
	if (Health)
	{
		Health->OnHealthChanged.AddUObject(this, &ThisClass::HealthChanged);
	}
	HealthChanged(Health ? Health->GetHealth() : 0.0f, Health ? Health->GetMaxHealth() : 0.0f);
}

void UDBHealthBarWidget::HealthChanged(float Current, float Maximum)
{
	HealthBar->SetPercent(Maximum > 0.0f ? FMath::Clamp(Current / Maximum, 0.0f, 1.0f) : 0.0f);
	SetVisibility(Current > 0.0f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UDBHealthBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	APlayerController* PC = GetOwningPlayer();
	AActor* Owner = HealthSource.IsValid() ? HealthSource->GetOwner() : nullptr;
	FVector ViewLocation;
	FRotator ViewRotation;
	if (PC)
	{
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	// Screen widgets have no depth test. Hide distant or obstructed owners explicitly.
	const bool bVisible =
	    PC && Owner && FVector::DistSquared(ViewLocation, Owner->GetActorLocation()) <= FMath::Square(MaxViewDistance)
	    && PC->LineOfSightTo(Owner, ViewLocation);
	SetRenderOpacity(bVisible ? 1.0f : 0.0f);
}

void UDBHealthBarWidget::NativeDestruct()
{
	BindHealth(nullptr);
	Super::NativeDestruct();
}
