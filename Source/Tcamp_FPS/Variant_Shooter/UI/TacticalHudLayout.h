// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/** Shared top-left HUD geometry, in Slate logical units. */
namespace TacticalHudLayout
{
	// UI_ShooterBulletCounter owns the Blueprint health bar at this geometry.
	constexpr float Left = 50.0f;
	constexpr float HealthTop = 50.0f;
	constexpr float HealthWidth = 300.0f;
	constexpr float HealthHeight = 25.0f;
	constexpr float VerticalGap = 5.0f;

	constexpr float ArmorWidth = 240.0f;
	constexpr float ArmorHeight = 16.0f;
	constexpr float ArmorTop = HealthTop - ArmorHeight - VerticalGap;

	constexpr float TeamBadgeWidth = 240.0f;
	constexpr float TeamBadgeHeight = 32.0f;
	constexpr float TeamBadgeTop = HealthTop + HealthHeight + 7.0f;
}
