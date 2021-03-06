/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "System/creg/STL_Map.h"
#include "WeaponDefHandler.h"
#include "Weapon.h"
#include "Game/GameHelper.h"
#include "Game/TraceRay.h"
#include "Game/Players/Player.h"
#include "Map/Ground.h"
#include "Sim/Misc/CollisionHandler.h"
#include "Sim/Misc/CollisionVolume.h"
#include "Sim/Misc/GeometricObjects.h"
#include "Sim/Misc/InterceptHandler.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/ModInfo.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/AAirMoveType.h"
#include "Sim/Projectiles/WeaponProjectiles/WeaponProjectile.h"
#include "Sim/Units/Scripts/CobInstance.h"
#include "Sim/Units/Scripts/NullUnitScript.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/Unit.h"
#include "Sim/Weapons/Cannon.h"
#include "System/EventHandler.h"
#include "System/float3.h"
#include "System/myMath.h"
#include "System/Sync/SyncTracer.h"
#include "System/Sound/ISoundChannels.h"
#include "System/Log/ILog.h"

CR_BIND_DERIVED(CWeapon, CObject, (NULL, NULL))

CR_REG_METADATA(CWeapon, (
	CR_MEMBER(owner),
	CR_MEMBER(aimFromPiece),
	CR_MEMBER(muzzlePiece),
	CR_MEMBER(range),
	CR_MEMBER(reloadTime),
	CR_MEMBER(reloadStatus),
	CR_MEMBER(salvoLeft),
	CR_MEMBER(salvoDelay),
	CR_MEMBER(salvoSize),
	CR_MEMBER(projectilesPerShot),
	CR_MEMBER(nextSalvo),
	CR_MEMBER(predict),
	CR_MEMBER(targetUnit),
	CR_MEMBER(accuracyError),
	CR_MEMBER(projectileSpeed),
	CR_MEMBER(predictSpeedMod),
	CR_MEMBER(fireSoundId),
	CR_MEMBER(fireSoundVolume),
	CR_MEMBER(hasBlockShot),
	CR_MEMBER(hasTargetWeight),
	CR_MEMBER(angleGood),
	CR_MEMBER(avoidTarget),
	CR_MEMBER(haveUserTarget),
	CR_MEMBER(onlyForward),
	CR_MEMBER(muzzleFlareSize),
	CR_MEMBER(doTargetGroundPos),
	CR_MEMBER(alreadyWarnedAboutMissingPieces),

	CR_MEMBER(badTargetCategory),
	CR_MEMBER(onlyTargetCategory),
	CR_MEMBER(incomingProjectiles),
	CR_MEMBER(weaponDef),
	CR_MEMBER(buildPercent),
	CR_MEMBER(numStockpiled),
	CR_MEMBER(numStockpileQued),
	CR_MEMBER(interceptTarget),
	CR_MEMBER(targetType),
	CR_MEMBER(sprayAngle),
	CR_MEMBER(useWeaponPosForAim),

	CR_MEMBER(lastRequest),
	CR_MEMBER(lastTargetRetry),

	CR_MEMBER(slavedTo),
	CR_MEMBER(maxForwardAngleDif),
	CR_MEMBER(maxMainDirAngleDif),
	CR_MEMBER(heightBoostFactor),
	CR_MEMBER(avoidFlags),
	CR_MEMBER(collisionFlags),
	CR_MEMBER(fuelUsage),
	CR_MEMBER(weaponNum),

	CR_MEMBER(relAimFromPos),
	CR_MEMBER(aimFromPos),
	CR_MEMBER(relWeaponMuzzlePos),
	CR_MEMBER(weaponMuzzlePos),
	CR_MEMBER(weaponDir),
	CR_MEMBER(mainDir),
	CR_MEMBER(wantedDir),
	CR_MEMBER(lastRequestedDir),
	CR_MEMBER(salvoError),
	CR_MEMBER(errorVector),
	CR_MEMBER(errorVectorAdd),
	CR_MEMBER(targetPos)
))

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CWeapon::CWeapon(CUnit* owner, const WeaponDef* def):
	owner(owner),
	weaponDef(def),
	weaponNum(-1),
	haveUserTarget(false),
	aimFromPiece(-1),
	muzzlePiece(-1),
	muzzleFlareSize(1),
	useWeaponPosForAim(0),
	reloadTime(1),
	reloadStatus(0),
	range(1),
	projectileSpeed(1),
	accuracyError(0),
	sprayAngle(0),
	salvoDelay(0),
	salvoSize(1),
	projectilesPerShot(1),
	nextSalvo(0),
	salvoLeft(0),
	targetType(Target_None),
	targetUnit(0),
	predict(0),
	predictSpeedMod(1),
	fireSoundId(0),
	fireSoundVolume(0),
	hasBlockShot(false),
	hasTargetWeight(false),
	angleGood(false),
	avoidTarget(false),
	onlyForward(false),
	doTargetGroundPos(false),
	alreadyWarnedAboutMissingPieces(false),
	badTargetCategory(0),
	onlyTargetCategory(0xffffffff),

	interceptTarget(NULL),
	buildPercent(0),
	numStockpiled(0),
	numStockpileQued(0),

	lastRequest(0),
	lastTargetRetry(-100),

	slavedTo(NULL),
	maxForwardAngleDif(0.0f),
	maxMainDirAngleDif(-1.0f),
	heightBoostFactor(-1.f),
	avoidFlags(0),
	collisionFlags(0),
	fuelUsage(0),

	relAimFromPos(UpVector),
	aimFromPos(ZeroVector),
	relWeaponMuzzlePos(UpVector),
	weaponMuzzlePos(ZeroVector),
	weaponDir(ZeroVector),
	mainDir(FwdVector),
	wantedDir(UpVector),
	lastRequestedDir(-UpVector),
	salvoError(ZeroVector),
	errorVector(ZeroVector),
	errorVectorAdd(ZeroVector),
	targetPos(OnesVector)
{
}


CWeapon::~CWeapon()
{
	if (weaponDef->interceptor)
		interceptHandler.RemoveInterceptorWeapon(this);
}


void CWeapon::SetWeaponNum(int num)
{
	weaponNum = num;

	UpdateWeaponPieces();
	UpdateWeaponVectors();
	hasBlockShot = owner->script->HasBlockShot(weaponNum);
	hasTargetWeight = owner->script->HasTargetWeight(weaponNum);
}


inline bool CWeapon::CobBlockShot() const
{
	if (!hasBlockShot) {
		return false;
	}

	return owner->script->BlockShot(weaponNum, targetUnit, haveUserTarget);
}


float CWeapon::TargetWeight(const CUnit* targetUnit) const
{
	return owner->script->TargetWeight(weaponNum, targetUnit);
}


void CWeapon::UpdateWeaponPieces(const bool updateAimFrom)
{
	// Call UnitScript
	muzzlePiece = owner->script->QueryWeapon(weaponNum);
	if (updateAimFrom) aimFromPiece = owner->script->AimFromWeapon(weaponNum);

	// Some UnitScripts only implement on of them
	const bool aimExists = owner->script->PieceExists(aimFromPiece);
	const bool muzExists = owner->script->PieceExists(muzzlePiece);
	if (aimExists && muzExists) {
		// everything fine
	} else
	if (!aimExists && muzExists) {
		aimFromPiece = muzzlePiece;
	} else
	if (aimExists && !muzExists) {
		muzzlePiece = aimFromPiece;
	} else
	if (!aimExists && !muzExists) {
		if (!alreadyWarnedAboutMissingPieces && (owner->script != &CNullUnitScript::value) && !weaponDef->isShield) {
			LOG_L(L_WARNING, "%s: weapon%i: Neither AimFromWeapon nor QueryWeapon defined or returned invalid pieceids", owner->unitDef->name.c_str(), weaponNum);
			alreadyWarnedAboutMissingPieces = true;
		}
		aimFromPiece = -1;
		muzzlePiece = -1;
	}

	// If we can't get a line of fire from the muzzle, try
	// the aim piece instead (since the weapon may just be
	// turned in a wrong way)
	//FIXME remove
	if (useWeaponPosForAim <= 0) {
		aimFromPiece = muzzlePiece;
	}
}


void CWeapon::UpdateWeaponVectors()
{
	relAimFromPos = owner->script->GetPiecePos(aimFromPiece);
	owner->script->GetEmitDirPos(muzzlePiece, relWeaponMuzzlePos, weaponDir);

	aimFromPos = owner->GetObjectSpacePos(relAimFromPos);
	weaponMuzzlePos = owner->GetObjectSpacePos(relWeaponMuzzlePos);
	weaponDir = owner->GetObjectSpaceVec(weaponDir).SafeNormalize();

	// hope that we are underground because we are a popup weapon and will come above ground later
	if (aimFromPos.y < CGround::GetHeightReal(aimFromPos.x, aimFromPos.z)) {
		aimFromPos = owner->pos + UpVector * 10;
	}
}


void CWeapon::UpdateWantedDir()
{
	if (!onlyForward) {
		wantedDir = (targetPos - aimFromPos).SafeNormalize();
	} else {
		wantedDir = owner->frontdir;
	}
}


void CWeapon::Update()
{
	UpdateWeaponVectors();

	if (!UpdateStockpile())
		return;

	UpdateTargeting();
	UpdateFire();
	UpdateSalvo();

#ifdef TRACE_SYNC
	tracefile << __FUNCTION__;
	tracefile << aimFromPos.x << " " << aimFromPos.y << " " << aimFromPos.z << " " << targetPos.x << " " << targetPos.y << " " << targetPos.z << "\n";
#endif
}


void CWeapon::UpdateTargeting()
{
	predict = std::min(predict, 50000.0f);
	errorVector += errorVectorAdd;

	if (weaponDef->interceptor) { //FIXME move to SlowUpdate()?
		// keep track of the closest projectile heading our way (if any)
		UpdateInterceptTarget();
	}

	if (!HaveTarget())
		return;

	AdjustTargetPosToWater(targetPos, targetType == Target_Pos);
	UpdateWantedDir();

	// Check fire angle constraints
	const float3 worldTargetDir = (targetPos - owner->pos).SafeNormalize();
	const float3 worldMainDir = owner->GetObjectSpaceVec(mainDir);
	const bool targetAngleConstraint = CheckTargetAngleConstraint(worldTargetDir, worldMainDir);
	if (angleGood && !targetAngleConstraint) {
		// weapon finished a previously started AimWeapon thread and wants to
		// fire, but target is no longer within contraints --> wait for re-aim
		angleGood = false;
	}
	if (onlyForward && targetAngleConstraint) {
		// NOTE:
		//   this should not need to be here, but many legacy scripts do not
		//   seem to define Aim*Ary in COB for units with onlyForward weapons
		//   (so angleGood is never set to true) -- REMOVE AFTER 90.0
		angleGood = true;
	}

	// reaim weapon when needed
	ReAimWeapon();
}


void CWeapon::ReAimWeapon()
{
	// NOTE:
	//   let scripts do active aiming even if we are an onlyForward weapon
	//   (reduces how far the entire unit must turn to face worldTargetDir)

	bool reAim = false;

	// periodically re-aim the weapon (by calling the script's AimWeapon
	// every N=15 frames regardless of current angleGood state)
	// if it does not (eg. because AimWeapon always spawns a thread to
	// aim the weapon and defers setting angleGood to it) then this can
	// lead to irregular/stuttering firing behavior, even in scenarios
	// when the weapon does not have to re-aim
	reAim |= (gs->frameNum >= (lastRequest + (GAME_SPEED >> 1)));

	// check max FireAngle
	reAim |= (wantedDir.dot(lastRequestedDir) <= weaponDef->maxFireAngle);
	reAim |= (wantedDir.dot(lastRequestedDir) <= math::cos(20.f));

	//note: angleGood checks unit/maindir, not the weapon's current aim dir!!!
	//reAim |= (!angleGood);

	if (!reAim)
		return;

		angleGood = false;

	lastRequestedDir = wantedDir;
	lastRequest = gs->frameNum;

	const float heading = GetHeadingFromVectorF(wantedDir.x, wantedDir.z);
	const float pitch = math::asin(Clamp(wantedDir.dot(owner->updir), -1.0f, 1.0f));

	// for COB, this sets <angleGood> to return value of AimWeapon when finished,
	// for LUS, there exists a callout to set the <angleGood> member directly.
	// FIXME: convert CSolidObject::heading to radians too.
	owner->script->AimWeapon(weaponNum, ClampRad(heading - owner->heading * TAANG2RAD), pitch);
}


bool CWeapon::CanFire(bool ignoreAngleGood, bool ignoreTargetType, bool ignoreRequestedDir) const
{
	// FIXME merge some of the checks with TryTarget/TestRange/TestTarget (!)
	if (!ignoreAngleGood && !angleGood)
		return false;

	if ((salvoLeft > 0) || (nextSalvo > gs->frameNum))
		return false;

	if (!ignoreTargetType && targetType == Target_None)
		return false;

	if (reloadStatus > gs->frameNum)
		return false;

	if (weaponDef->stockpile && numStockpiled == 0)
		return false;

	// muzzle is underwater but we cannot fire underwater
	if (!weaponDef->fireSubmersed && weaponMuzzlePos.y <= 0.0f)
		return false;

	// sanity check to force new aim
	if (weaponDef->maxFireAngle > -1.0f) {
		if (!ignoreRequestedDir && wantedDir.dot(lastRequestedDir) <= weaponDef->maxFireAngle)
			return false;
	}

	if ((fuelUsage > 0.0f) && (owner->currentFuel <= 0.0f))
		return false;

	// if in FPS mode, player must be pressing at least one button to fire
	const CPlayer* fpsPlayer = owner->fpsControlPlayer;
	if (fpsPlayer != NULL && !fpsPlayer->fpsController.mouse1 && !fpsPlayer->fpsController.mouse2)
		return false;

	// FIXME: there is already CUnit::dontUseWeapons but only used by HoverAirMoveType when landed
	const AAirMoveType* airMoveType = dynamic_cast<AAirMoveType*>(owner->moveType);
	if (airMoveType != NULL && airMoveType->GetPadStatus() != AAirMoveType::PAD_STATUS_FLYING)
		return false;

	return true;
}

void CWeapon::UpdateFire()
{
	if (!CanFire(false, false, false))
		return;

	CTeam* ownerTeam = teamHandler->Team(owner->team);

	if ((weaponDef->stockpile || (ownerTeam->res.metal >= weaponDef->metalcost && ownerTeam->res.energy >= weaponDef->energycost))) {
		owner->script->GetEmitDirPos(owner->script->QueryWeapon(weaponNum), relWeaponMuzzlePos, weaponDir);

		weaponMuzzlePos = owner->GetObjectSpacePos(relWeaponMuzzlePos);
		weaponDir = owner->GetObjectSpaceVec(weaponDir);
		weaponDir.SafeNormalize();
		useWeaponPosForAim = (reloadTime / UNIT_SLOWUPDATE_RATE) + 8;

		if (TryTarget(targetPos, haveUserTarget, targetUnit) && !CobBlockShot()) {
			if (weaponDef->stockpile) {
				const int oldCount = numStockpiled;
				numStockpiled--;
				owner->commandAI->StockpileChanged(this);
				eventHandler.StockpileChanged(owner, this, oldCount);
			} else {
				owner->UseEnergy(weaponDef->energycost);
				owner->UseMetal(weaponDef->metalcost);
				owner->currentFuel = std::max(0.0f, owner->currentFuel - fuelUsage);
			}

			reloadStatus = gs->frameNum + int(reloadTime / owner->reloadSpeed);

			salvoLeft = salvoSize;
			nextSalvo = gs->frameNum;
			salvoError = gs->randVector() * (owner->IsMoving()? weaponDef->movingAccuracy: accuracyError);

			if (targetType == Target_Pos || (targetType == Target_Unit && !(targetUnit->losStatus[owner->allyteam] & LOS_INLOS))) {
				// area firing stuff is too effective at radar firing...
				salvoError *= 1.3f;
			}

			owner->lastMuzzleFlameSize = muzzleFlareSize;
			owner->lastMuzzleFlameDir = wantedDir;
			owner->script->FireWeapon(weaponNum);
		}
	} else {
		if (!weaponDef->stockpile && TryTarget(targetPos, haveUserTarget, targetUnit)) {
			// update the energy and metal required counts
			const int minPeriod = std::max(1, (int)(reloadTime / owner->reloadSpeed));
			const float averageFactor = 1.0f / minPeriod;

			ownerTeam->resPull.energy += (averageFactor * weaponDef->energycost);
			ownerTeam->resPull.metal  += (averageFactor * weaponDef->metalcost);
		}
	}
}


bool CWeapon::UpdateStockpile()
{
	if (!weaponDef->stockpile)
		return true;

	if (numStockpileQued > 0) {
		const float p = 1.0f / weaponDef->stockpileTime;

		if (teamHandler->Team(owner->team)->res.metal >= weaponDef->metalcost*p && teamHandler->Team(owner->team)->res.energy >= weaponDef->energycost*p) {
			owner->UseEnergy(weaponDef->energycost * p);
			owner->UseMetal(weaponDef->metalcost * p);
			buildPercent += p;
		} else {
			// update the energy and metal required counts
			teamHandler->Team(owner->team)->resPull.energy += (weaponDef->energycost * p);
			teamHandler->Team(owner->team)->resPull.metal  += (weaponDef->metalcost * p);
		}
		if (buildPercent >= 1) {
			const int oldCount = numStockpiled;
			buildPercent = 0;
			numStockpileQued--;
			numStockpiled++;
			owner->commandAI->StockpileChanged(this);
			eventHandler.StockpileChanged(owner, this, oldCount);
		}
	}

	if (numStockpiled <= 0 && salvoLeft <= 0) {
		return false;
	}
	return true;
}


void CWeapon::UpdateSalvo()
{
	if (!salvoLeft || nextSalvo > gs->frameNum)
		return;

	salvoLeft--;
	nextSalvo = gs->frameNum + salvoDelay;

	// Decloak
	if ((owner->scriptCloak <= 2) && owner->unitDef->decloakOnFire) {
		if (owner->isCloaked) {
			owner->isCloaked = false;
			eventHandler.UnitDecloaked(owner);
		}
		owner->curCloakTimeout = gs->frameNum + owner->cloakTimeout;
	}

	for (int i = 0; i < projectilesPerShot; ++i) {
		owner->script->Shot(weaponNum);

		// Update Muzzle Piece/Pos
		UpdateWeaponPieces(false);
		UpdateWeaponVectors();

		Fire(false);
	}

	// Rock the unit in the direction of fire
	if (owner->script->HasRockUnit()) {
		const float3 rockDir = (-wantedDir).SafeNormalize2D();
		owner->script->RockUnit(rockDir);
	}

	const bool attackingPos  = ((targetType == Target_Pos ) && (targetPos  == owner->attackPos));
	const bool attackingUnit = ((targetType == Target_Unit) && (targetUnit == owner->attackTarget));
	const bool searchForNewTarget = (weaponNum == 0) && (salvoLeft == 0) && (attackingPos || attackingUnit);
	owner->commandAI->WeaponFired(this, searchForNewTarget);

	if (salvoLeft == 0) {
		owner->script->EndBurst(weaponNum);
	}
}

bool CWeapon::AttackGround(float3 newTargetPos, bool isUserTarget)
{
	if (!isUserTarget && weaponDef->noAutoTarget)
		return false;
	if (weaponDef->interceptor || !weaponDef->canAttackGround)
		return false;

	// keep target positions on the surface if this weapon hates water
	AdjustTargetPosToWater(newTargetPos, true);

	// prevent range hax in FPS mode
	if (owner->UnderFirstPersonControl() && dynamic_cast<CCannon*>(this)) {
		newTargetPos.y = CGround::GetHeightAboveWater(newTargetPos.x, newTargetPos.z);
	}

	UpdateWeaponVectors();

	if (!TryTarget(newTargetPos, isUserTarget, nullptr))
		return false;

	if (targetUnit != NULL) {
		DeleteDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
		targetUnit = NULL;
	}

	haveUserTarget = isUserTarget;
	targetType = Target_Pos;
	targetPos = newTargetPos;

	return true;
}

bool CWeapon::AttackUnit(CUnit* newTargetUnit, bool isUserTarget)
{
	if (!isUserTarget && weaponDef->noAutoTarget) {
		return false;
	}
	if (weaponDef->interceptor)
		return false;

	UpdateWeaponVectors();

	if (newTargetUnit == NULL) {
		if (targetType != Target_Unit) {
			// make the unit be more likely to keep the current target if user starts to move it
			targetType = Target_None;
		}

		// cannot have a user-target without a unit
		haveUserTarget = false;
		return false;
	}

	const float3 newTargetPos = GetUnitLeadTargetPos(newTargetUnit);

	if (!TryTarget(newTargetPos, isUserTarget, newTargetUnit))
		return false;

	if (targetUnit != NULL) {
		DeleteDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
		targetUnit = NULL;
	}

	haveUserTarget = isUserTarget;
	targetType = Target_Unit;
	targetUnit = newTargetUnit;
	targetPos = newTargetPos;

	AddDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
	avoidTarget = false;

	return true;
}


void CWeapon::HoldFire()
{
	if (targetUnit) {
		DeleteDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
		targetUnit = NULL;
	}

	targetType = Target_None;

	if (!weaponDef->noAutoTarget) {
		// if haveUserTarget is set to false unconditionally, a subsequent
		// call to AttackUnit from Unit::SlowUpdateWeapons would abort the
		// attack for noAutoTarget weapons
		haveUserTarget = false;
	}
}



bool CWeapon::AllowWeaponTargetCheck()
{
	const int checkAllowed = eventHandler.AllowWeaponTargetCheck(owner->id, weaponNum, weaponDef->id);

	if (checkAllowed >= 0) {
		return checkAllowed;
	}

	if (weaponDef->noAutoTarget)                 { return false; }
	if (owner->fireState < FIRESTATE_FIREATWILL) { return false; }

	// if CAI has an auto-generated attack order, do not interfere
	if (!owner->commandAI->CanWeaponAutoTarget())
		return false;

	if (avoidTarget)               { return true; }
	if (targetType == Target_None) { return true; }

	if (targetType == Target_Unit) {
		if (targetUnit->category & badTargetCategory) {
			return true;
		}
		if (!TryTarget(targetUnit, haveUserTarget)) {
			// if we have a user-target (ie. a user attack order)
			// then only allow generating opportunity targets iff
			// it is not possible to hit the user's chosen unit
			// TODO: this makes it easy to add toggle-able locking
			//
			// this will switch <targetUnit>, but the CAI will keep
			// calling AttackUnit while the original order target is
			// alive to put it back when possible
			//
			// note that the CAI itself only auto-picks a target
			// when a unit has no commands left in its queue, so
			// it can not interfere
			return true;
		}
	}

	if (gs->frameNum > (lastTargetRetry + 65)) {
		return true;
	}

	return false;
}

void CWeapon::AutoTarget()
{
	lastTargetRetry = gs->frameNum;

	std::multimap<float, CUnit*> targets;
	std::multimap<float, CUnit*>::const_iterator targetsIt;

	//FIXME ignore and not avoid!
	const CUnit* avoidUnit = (targetType == Target_Unit) ? targetUnit : nullptr;

	// NOTE:
	//   sorts by INCREASING order of priority, so lower equals better
	//   <targets> can contain duplicates if a unit covers multiple quads
	//   <targets> is normally sorted such that all bad TC units are at the
	//   end, but Lua can mess with the ordering arbitrarily
	CGameHelper::GenerateWeaponTargets(this, avoidUnit, targets);

	CUnit* goodTargetUnit = nullptr;
	CUnit* badTargetUnit = nullptr;

	for (targetsIt = targets.begin(); targetsIt != targets.end(); ++targetsIt) {
		CUnit* unit = targetsIt->second;

		// save the "best" bad target in case we have no other
		// good targets (of higher priority) left in <targets>
		const bool isBadTarget = (unit->category & badTargetCategory);
		if (isBadTarget && !badTargetUnit)
			continue;

		const float3 nextTargetPos = GetUnitLeadTargetPos(unit);
		if (!TryTarget(nextTargetPos, false, unit))
			continue;

		if (unit->IsNeutral() && (owner->fireState <= FIRESTATE_FIREATWILL))
			continue;

		if (isBadTarget) {
			badTargetUnit = unit;
		} else {
			goodTargetUnit = unit;
			break;
		}
	}

	if (goodTargetUnit != NULL || badTargetUnit != NULL) {
		const bool haveOldTarget = (targetUnit != NULL);
		const bool haveNewTarget =
			(goodTargetUnit != NULL && goodTargetUnit != targetUnit) ||
			( badTargetUnit != NULL &&  badTargetUnit != targetUnit);

		if (haveOldTarget && haveNewTarget) {
			// delete our old target dependence if we are switching targets
			DeleteDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
		}

		// pick our new target
		targetType = Target_Unit;
		targetUnit = (goodTargetUnit != NULL)? goodTargetUnit: badTargetUnit;
		targetPos = GetUnitLeadTargetPos(targetUnit);

		if (!haveOldTarget || haveNewTarget) {
			// add new target dependence if we had no target or switched
			AddDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
		}
	}
}


void CWeapon::SlowUpdate()
{
	SlowUpdate(false);
}

void CWeapon::SlowUpdate(bool noAutoTargetOverride)
{
	errorVectorAdd = (gs->randVector() - errorVector) * (1.0f / UNIT_SLOWUPDATE_RATE);

#ifdef TRACE_SYNC
	tracefile << "Weapon slow update: ";
	tracefile << owner->id << " " << weaponNum <<  "\n";
#endif

	UpdateWeaponPieces();
	UpdateWeaponVectors();
	useWeaponPosForAim = std::max(0, useWeaponPosForAim - 1);

	predictSpeedMod = 1.0f + (gs->randFloat() - 0.5f) * 2 * (1.0f - owner->limExperience);


	if (targetType != Target_None && !TryTarget(targetPos, haveUserTarget, targetUnit)) {
		HoldFire();
	}

	if (targetType == Target_Unit) {
		// stop firing at cloaked targets
		if (targetUnit != NULL && targetUnit->isCloaked && !(targetUnit->losStatus[owner->allyteam] & (LOS_INLOS | LOS_INRADAR)))
			HoldFire();

		if (!haveUserTarget) {
			// stop firing at neutral targets (unless in FAW mode)
			// note: HoldFire sets targetUnit to NULL, so recheck
			if (targetUnit != NULL && targetUnit->IsNeutral() && owner->fireState <= FIRESTATE_FIREATWILL)
				HoldFire();

			// stop firing at allied targets
			//
			// this situation (unit keeps attacking its target if the
			// target or the unit switches to an allied team) should
			// be handled by /ally processing now
			if (targetUnit != NULL && teamHandler->Ally(owner->allyteam, targetUnit->allyteam))
				HoldFire();
		}
	}

	if (slavedTo != NULL) {
		// use targets from the thing we are slaved to
		if (targetUnit) {
			DeleteDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
			targetUnit = NULL;
		}
		targetType = Target_None;

		if (slavedTo->targetType == Target_Unit) {
			const float errorScale = (MoveErrorExperience() * GAME_SPEED * slavedTo->targetUnit->speed.w);

			const float3 errorPos = slavedTo->targetUnit->GetErrorPos(owner->allyteam, true);
			const float3 targetErrPos = errorPos + errorVector * errorScale;

			if (TryTarget(targetErrPos, false, slavedTo->targetUnit)) {
				targetType = Target_Unit;
				targetUnit = slavedTo->targetUnit;
				targetPos = targetErrPos;

				AddDeathDependence(targetUnit, DEPENDENCE_TARGETUNIT);
			}
		} else if (slavedTo->targetType == Target_Pos) {
			if (TryTarget(slavedTo->targetPos, false, 0)) {
				targetType = Target_Pos;
				targetPos = slavedTo->targetPos;
			}
		}
		return;
	}


	if (!noAutoTargetOverride && AllowWeaponTargetCheck()) {
		AutoTarget();
	}

	if (targetType == Target_None) {
		// if we can't target anything, try switching aim point
		useWeaponPosForAim = std::max(0, useWeaponPosForAim - 1);
	}
}

void CWeapon::DependentDied(CObject* o)
{
	if (o == targetUnit) {
		targetUnit = NULL;
		if (targetType == Target_Unit) {
			targetType = Target_None;
			haveUserTarget = false;
		}
	}

	// NOTE: DependentDied is called from ~CObject-->Detach, object is just barely valid
	if (weaponDef->interceptor || weaponDef->isShield) {
		incomingProjectiles.erase(static_cast<CWeaponProjectile*>(o)->id);
	}

	if (o == interceptTarget) {
		interceptTarget = NULL;
	}
}

bool CWeapon::TargetUnitOrPositionUnderWater(const float3 targetPos, const CUnit* targetUnit, float offset)
{
	// test if a target position or unit is strictly underwater
	if (targetUnit != NULL) {
		return (targetUnit->IsUnderWater());
	} else {
		// consistent with CSolidObject::IsUnderWater (LT)
		return ((targetPos.y + offset) < 0.0f);
	}
}

bool CWeapon::TargetUnitOrPositionInWater(const float3 targetPos, const CUnit* targetUnit, float offset)
{
	// test if a target position or unit is in water (including underwater)
	if (targetUnit != NULL) {
		return (targetUnit->IsInWater());
	} else {
		// consistent with CSolidObject::IsInWater (LE)
		return ((targetPos.y + offset) <= 0.0f);
	}
}

bool CWeapon::CheckTargetAngleConstraint(const float3 worldTargetDir, const float3 worldWeaponDir) const {
	if (onlyForward) {
		if (maxForwardAngleDif > -1.0f) {
			// if we are not a turret, we care about our owner's direction
			if (owner->frontdir.dot(worldTargetDir) < maxForwardAngleDif)
				return false;
		}
	} else {
		if (maxMainDirAngleDif > -1.0f) {
			if (worldWeaponDir.dot(worldTargetDir) < maxMainDirAngleDif)
				return false;
		}
	}

	return true;
}


float3 CWeapon::GetTargetBorderPos(
	const CUnit* targetUnit,
	const float3 rawTargetPos,
	const float3 rawTargetDir) const
{
	float3 targetBorderPos = rawTargetPos;

	if (weaponDef->targetBorder == 0.0f)
		return targetBorderPos;
	if (targetUnit == NULL)
		return targetBorderPos;
	if (rawTargetDir == ZeroVector)
		return targetBorderPos;

	const float tbScale = math::fabsf(weaponDef->targetBorder);

	CollisionVolume  tmpColVol = CollisionVolume(targetUnit->collisionVolume);
	CollisionQuery   tmpColQry;

	// test for "collision" with a temporarily volume
	// (scaled uniformly by the absolute target-border
	// factor)
	tmpColVol.RescaleAxes(float3(tbScale, tbScale, tbScale));
	tmpColVol.SetBoundingRadius();
	tmpColVol.SetUseContHitTest(false);

	if (CCollisionHandler::DetectHit(&tmpColVol, targetUnit, weaponMuzzlePos, ZeroVector, NULL)) { //FIXME use aimFromPos ?
		// our weapon muzzle is inside the target unit's volume
		targetBorderPos = weaponMuzzlePos;
	} else {
		// otherwise, perform a raytrace to find the proper length correction
		// factor for non-spherical coldet volumes based on the ray's ingress
		// (for positive TB values) or egress (for negative TB values) position;
		// this either increases or decreases the length of <targetVec> but does
		// not change its direction
		tmpColVol.SetUseContHitTest(true);

		// make the ray-segment long enough so it can reach the far side of the
		// scaled collision volume (helps to ensure a ray-intersection is found)
		//
		// note: ray-intersection is NOT guaranteed if the volume itself has a
		// non-zero offset, since here we are "shooting" at the target UNIT's
		// aimpoint
		const float3 targetOffset = rawTargetDir * (tmpColVol.GetBoundingRadius() * 2.0f);
		const float3 targetRayPos = rawTargetPos + targetOffset;

		// adjust the length of <targetVec> based on the targetBorder factor
		if (CCollisionHandler::DetectHit(&tmpColVol, targetUnit, weaponMuzzlePos, targetRayPos, &tmpColQry)) {
			targetBorderPos = (weaponDef->targetBorder > 0.0f) ? tmpColQry.GetIngressPos() : tmpColQry.GetEgressPos();
		}
	}

	return targetBorderPos;
}


bool CWeapon::TryTarget(const float3 tgtPos, bool userTarget, const CUnit* targetUnit) const
{
	if (!TestTarget(tgtPos, userTarget, targetUnit))
		return false;

	if (!TestRange(tgtPos, userTarget, targetUnit))
		return false;

	//FIXME add a forcedUserTarget (a forced fire mode enabled with ctrl key or something) and skip the tests below then
	return (HaveFreeLineOfFire(tgtPos, userTarget, targetUnit));
}


// if targetUnit != NULL, this checks our onlyTargetCategory against unit->category
// etc. as well as range, otherwise the only concern is range and angular difference
// (terrain is NOT checked here, HaveFreeLineOfFire does that)
bool CWeapon::TestTarget(const float3 tgtPos, bool /*userTarget*/, const CUnit* targetUnit) const
{
	if (targetUnit != NULL) {
		if (targetUnit == owner)
			return false;
		if ((targetUnit->category & onlyTargetCategory) == 0)
			return false;
		if (targetUnit->isDead && modInfo.fireAtKilled == 0)
			return false;
		if (targetUnit->IsCrashing() && modInfo.fireAtCrashing == 0)
			return false;
	}

	if (!weaponDef->waterweapon) {
		// we cannot pick targets underwater, check where target is in relation to us
		if (!owner->IsUnderWater() && TargetUnitOrPositionUnderWater(tgtPos, targetUnit))
			return false;
		// if we are underwater but target is *not* in water, fireSubmersed gets checked
		if (owner->IsUnderWater() && TargetUnitOrPositionInWater(tgtPos, targetUnit))
			return false;
	}

	return true;
}

bool CWeapon::TestRange(const float3 tgtPos, bool /*userTarget*/, const CUnit* targetUnit) const
{
	const float3 tmpTargetDir = (tgtPos - aimFromPos).SafeNormalize();

	const float heightDiff = tgtPos.y - owner->pos.y;
	float weaponRange = 0.0f; // range modified by heightDiff and cylinderTargeting

	if (targetUnit == NULL || weaponDef->cylinderTargeting < 0.01f) {
		// check range in a sphere (with extra radius <heightDiff * heightMod>)
		weaponRange = GetRange2D(heightDiff * weaponDef->heightmod);
	} else {
		// check range in a cylinder (with height <cylinderTargeting * range>)
		if ((weaponDef->cylinderTargeting * range) > (math::fabsf(heightDiff) * weaponDef->heightmod)) {
			weaponRange = GetRange2D(0.0f);
		}
	}

	if (aimFromPos.SqDistance2D(tgtPos) >= (weaponRange * weaponRange))
		return false;

	// NOTE: mainDir is in unit-space
	const float3 worldMainDir = owner->GetObjectSpaceVec(mainDir);

	return (CheckTargetAngleConstraint(tmpTargetDir, worldMainDir));
}


bool CWeapon::HaveFreeLineOfFire(const float3 pos, bool userTarget, const CUnit* unit) const
{
	float3 dir = pos - weaponMuzzlePos;

	const float length = dir.Length();
	const float spread = AccuracyExperience() + SprayAngleExperience();

	if (length == 0.0f)
		return true;

	dir /= length;

	// ground check
	if ((avoidFlags & Collision::NOGROUND) == 0) {
		// NOTE:
		//     ballistic weapons (Cannon / Missile icw. trajectoryHeight) do not call this,
		//     they use TrajectoryGroundCol with an external check for the NOGROUND flag
		CUnit* unit = NULL;
		CFeature* feature = NULL;

		const float gdst = TraceRay::TraceRay(weaponMuzzlePos, dir, length, ~Collision::NOGROUND, owner, unit, feature);
		const float3 gpos = weaponMuzzlePos + dir * gdst;

		// true iff ground does not block the ray of length <length> from <pos> along <dir>
		if ((gdst > 0.0f) && (gpos.SqDistance(pos) > Square(weaponDef->damageAreaOfEffect)))
			return false;
	}

	// friendly, neutral & feature check
	if (TraceRay::TestCone(weaponMuzzlePos, dir, length, spread, owner->allyteam, avoidFlags, owner)) {
		return false;
	}

	return true;
}


bool CWeapon::TryTarget(const CUnit* unit, bool userTarget) const {
	return TryTarget(GetUnitPositionWithError(unit), userTarget, unit);
}


bool CWeapon::TryTargetRotate(const CUnit* unit, bool userTarget)
{
	const float3 tempTargetPos = GetUnitLeadTargetPos(unit);
	const short weaponHeading = GetHeadingFromVector(mainDir.x, mainDir.z);
	const short enemyHeading = GetHeadingFromVector(tempTargetPos.x - aimFromPos.x, tempTargetPos.z - aimFromPos.z);

	return TryTargetHeading(enemyHeading - weaponHeading, tempTargetPos, userTarget, unit);
}


bool CWeapon::TryTargetRotate(float3 pos, bool userTarget)
{
	if (!userTarget && weaponDef->noAutoTarget)
		return false;
	if (weaponDef->interceptor || !weaponDef->canAttackGround)
		return false;

	AdjustTargetPosToWater(pos, true);

	const short weaponHeading = GetHeadingFromVector(mainDir.x, mainDir.z);
	const short enemyHeading = GetHeadingFromVector(
		pos.x - aimFromPos.x, pos.z - aimFromPos.z);

	return TryTargetHeading(enemyHeading - weaponHeading, pos, userTarget, 0);
}


bool CWeapon::TryTargetHeading(short heading, float3 pos, bool userTarget, const CUnit* unit)
{
	const float3 tempfrontdir(owner->frontdir);
	const float3 temprightdir(owner->rightdir);
	const short tempHeading = owner->heading;

	AdjustTargetPosToWater(pos, unit == nullptr);

	owner->heading = heading;
	owner->frontdir = GetVectorFromHeading(owner->heading);
	owner->rightdir = owner->frontdir.cross(owner->updir);
	UpdateWeaponVectors();

	const bool val = TryTarget(pos, userTarget, unit);

	owner->frontdir = tempfrontdir;
	owner->rightdir = temprightdir;
	owner->heading = tempHeading;
	UpdateWeaponVectors();

	return val;

}

void CWeapon::Init()
{
	UpdateWeaponPieces();
	UpdateWeaponVectors();

	muzzleFlareSize = std::min(weaponDef->damageAreaOfEffect * 0.2f, std::min(1500.f, weaponDef->damages[0]) * 0.003f);

	if (weaponDef->interceptor)
		interceptHandler.AddInterceptorWeapon(this);

	if (weaponDef->stockpile) {
		owner->stockpileWeapon = this;
		owner->commandAI->AddStockpileWeapon(this);
	}

	if (weaponDef->isShield) {
		if ((owner->shieldWeapon == NULL) ||
		    (owner->shieldWeapon->weaponDef->shieldRadius < weaponDef->shieldRadius)) {
			owner->shieldWeapon = this;
		}
	}
}

void CWeapon::Fire(bool scriptCall)
{
#ifdef TRACE_SYNC
	tracefile << weaponDef->name.c_str() << " fire: ";
	tracefile << owner->pos.x << " " << owner->frontdir.x << " " << targetPos.x << " " << targetPos.y << " " << targetPos.z;
	tracefile << sprayAngle << " " <<  " " << salvoError.x << " " << salvoError.z << " " << owner->limExperience << " " << projectileSpeed << "\n";
#endif
	owner->lastFireWeapon = gs->frameNum;

	FireImpl(scriptCall);

	if (fireSoundId > 0 && (!weaponDef->soundTrigger || salvoLeft == salvoSize - 1)) {
		Channels::Battle->PlaySample(fireSoundId, owner, fireSoundVolume);
	}
}

void CWeapon::UpdateInterceptTarget()
{
	targetType = Target_None;

	float minInterceptTargetDistSq = std::numeric_limits<float>::max();
	float curInterceptTargetDistSq = std::numeric_limits<float>::min();

	for (std::map<int, CWeaponProjectile*>::iterator pi = incomingProjectiles.begin(); pi != incomingProjectiles.end(); ++pi) {
		CWeaponProjectile* p = pi->second;

		// set by CWeaponProjectile's ctor when the interceptor fires
		if (p->IsBeingIntercepted())
			continue;
		if ((curInterceptTargetDistSq = (p->pos - aimFromPos).SqLength()) >= minInterceptTargetDistSq)
			continue;

		minInterceptTargetDistSq = curInterceptTargetDistSq;

		// trigger us to auto-fire at this incoming projectile
		// we do not really need to set targetPos here since it
		// will be read from params.target (GetProjectileParams)
		// when our subclass Fire()'s
		interceptTarget = p;
		targetType = Target_Intercept;
		targetPos = p->pos + p->speed;
	}
}


ProjectileParams CWeapon::GetProjectileParams()
{
	ProjectileParams params;

	if (interceptTarget != NULL) {
		params.target = interceptTarget;
	} else {
		params.target = targetUnit;
	}

	params.weaponID = weaponNum;
	params.owner = owner;
	params.weaponDef = weaponDef;

	return params;
}


float CWeapon::GetRange2D(float yDiff) const
{
	const float root1 = range * range - yDiff * yDiff;
	return (root1 > 0.0f) ? math::sqrt(root1) : 0.0f;
}


void CWeapon::StopAttackingAllyTeam(int ally)
{
	if (targetUnit && targetUnit->allyteam == ally) {
		HoldFire();
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

// NOTE:
//   GUIHandler places (some) user ground-attack orders on the
//   water surface, others on the ocean floor and in both cases
//   without examining weapon abilities (its logic is "obtuse")
//
//   this inconsistency would be hard(er) to fix on the UI side
//   so we must adjust all such target positions in synced code
//
//   see also CommandAI::AdjustGroundAttackCommand
void CWeapon::AdjustTargetPosToWater(float3& tgtPos, bool attackGround) const
{
	if (!attackGround)
		return;

	tgtPos.y = std::max(tgtPos.y, CGround::GetHeightReal(tgtPos.x, tgtPos.z));
	tgtPos.y = std::max(tgtPos.y, tgtPos.y * weaponDef->waterweapon);
}


float3 CWeapon::GetUnitPositionWithError(const CUnit* unit) const
{
	float3 errorPos = unit->GetErrorPos(owner->allyteam, true);
	if (doTargetGroundPos) errorPos -= unit->aimPos - unit->pos;
	const float errorScale = (MoveErrorExperience() * GAME_SPEED * unit->speed.w);
	return errorPos + errorVector * errorScale;
}


float3 CWeapon::GetUnitLeadTargetPos(const CUnit* unit) const
{
	const float3 tmpTargetPos = GetUnitPositionWithError(unit) + GetLeadVec(unit);
	const float3 tmpTargetDir = (tmpTargetPos - aimFromPos).SafeNormalize();

	float3 aimPos = GetTargetBorderPos(unit, tmpTargetPos, tmpTargetDir);

	// never target below terrain
	// never target below water if not a water-weapon
	aimPos.y = std::max(aimPos.y, CGround::GetApproximateHeight(aimPos.x, aimPos.z) + 2.0f);
	aimPos.y = std::max(aimPos.y, aimPos.y * weaponDef->waterweapon);

	return aimPos;
}


float3 CWeapon::GetLeadVec(const CUnit* unit) const
{
	float3 lead = unit->speed * predict * mix(predictSpeedMod, 1.0f, weaponDef->predictBoost);
	if (weaponDef->leadLimit >= 0.0f) {
		const float leadBonus = weaponDef->leadLimit + weaponDef->leadBonus * owner->experience;
		if (lead.SqLength() > Square(leadBonus))
			lead *= (leadBonus) / (lead.Length() + 0.01f);
	}
	return lead;
}


float CWeapon::ExperienceErrorScale() const
{
	// accuracy (error) is increased (decreased) with experience
	// scale is 1.0f - (limExperience * expAccWeight), such that
	// for weight=0 scale is 1 and for weight=1 scale is 1 - exp
	// (lower is better)
	//
	//   for accWeight=0.00 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*0.00, 0.5*0.00, 0.75*0.00, 1.0*0.00}) = {1.0000, 1.000, 1.0000, 1.00}
	//   for accWeight=0.25 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*0.25, 0.5*0.25, 0.75*0.25, 1.0*0.25}) = {0.9375, 0.875, 0.8125, 0.75}
	//   for accWeight=0.50 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*0.50, 0.5*0.50, 0.75*0.50, 1.0*0.50}) = {0.8750, 0.750, 0.6250, 0.50}
	//   for accWeight=1.00 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*1.00, 0.5*1.00, 0.75*1.00, 1.0*0.75}) = {0.7500, 0.500, 0.2500, 0.25}
	return (CUnit::ExperienceScale(owner->limExperience, weaponDef->ownerExpAccWeight));
}

float CWeapon::MoveErrorExperience() const
{
	return (ExperienceErrorScale() * weaponDef->targetMoveError);
}

