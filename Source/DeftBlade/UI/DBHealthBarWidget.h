#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DBHealthBarWidget.generated.h"

class UDBHealthComponent;
class UProgressBar;

/** Screen-space overhead health; the health component remains authoritative. */
UCLASS(Abstract)
class DEFTBLADE_API UDBHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void BindHealth(UDBHealthComponent* Health);

protected:
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "0", Units = "cm"))
	float MaxViewDistance = 2500.0f;

private:
	void HealthChanged(float Current, float Maximum);
	TWeakObjectPtr<UDBHealthComponent> HealthSource;
};
