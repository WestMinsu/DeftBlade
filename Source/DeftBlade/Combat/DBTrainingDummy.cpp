#include "Combat/DBTrainingDummy.h"

#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADBTrainingDummy::ADBTrainingDummy()
{
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(32.0f, 90.0f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	Capsule->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Overlap);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetRelativeScale3D(FVector(0.55f, 0.55f, 1.8f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Body->SetStaticMesh(Cube.Object);
	}
	HealthText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HealthText"));
	HealthText->SetupAttachment(Capsule);
	HealthText->SetRelativeLocation(FVector(0, 0, 120));
	HealthText->SetHorizontalAlignment(EHTA_Center);
	HealthText->SetWorldSize(24.0f);
	HealthText->SetTextRenderColor(FColor::White);
	HealthComponent = CreateDefaultSubobject<UDBHealthComponent>(TEXT("Health"));
}

void ADBTrainingDummy::BeginPlay()
{
	Super::BeginPlay();
	BodyMaterial = Body->CreateDynamicMaterialInstance(0);
	RestoreColor();
	HealthComponent->OnHealthChanged.AddUObject(this, &ThisClass::UpdateHealthText);
	HealthComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	UpdateHealthText(HealthComponent->GetHealth(), HealthComponent->GetMaxHealth());
}

float ADBTrainingDummy::TakeDamage(
    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	const float Applied = HealthComponent->ApplyDamage(Amount);
	if (Applied > 0.0f)
	{
		Super::TakeDamage(Applied, Event, EventInstigator, Causer);
		if (!HealthComponent->IsDead())
		{
			if (BodyMaterial)
			{
				BodyMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.12f, 0.025f));
			}
			GetWorldTimerManager().SetTimer(FeedbackTimer, this, &ThisClass::RestoreColor, 0.12f, false);
		}
	}
	return Applied;
}

void ADBTrainingDummy::UpdateHealthText(float Current, float Maximum)
{
	HealthText->SetText(FText::FromString(FString::Printf(TEXT("HP %.0f / %.0f"), Current, Maximum)));
}

void ADBTrainingDummy::RestoreColor()
{
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.04f, 0.3f, 0.65f));
	}
}

void ADBTrainingDummy::HandleDeath()
{
	GetWorldTimerManager().ClearTimer(FeedbackTimer);
	SetActorEnableCollision(false);
	HealthText->SetText(FText::FromString(TEXT("HP 0 - DOWN")));
	HealthText->SetTextRenderColor(FColor(180, 180, 180));
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.09f, 0.09f, 0.09f));
	}
}

void ADBTrainingDummy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FeedbackTimer);
	Super::EndPlay(EndPlayReason);
}
