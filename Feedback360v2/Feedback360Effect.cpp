/*
    MICE Xbox 360 Controller driver for Mac OS X
    Force Feedback module
    Copyright (C) 2013 David Ryskalczyk
    Based on xi, Copyright (C) 2011 ledyba

    Feedback360Effect.cpp - Main code for the FF plugin
    
    This file is part of Xbox360Controller.

    Xbox360Controller is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Xbox360Controller is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Feedback360Effect.h"

//----------------------------------------------------------------------------------------------
// CEffect
//----------------------------------------------------------------------------------------------
Feedback360Effect::Feedback360Effect()
{
    Type  = NULL;
    memset(&DiEffect, 0, sizeof(FFEFFECT));
    memset(&DiEnvelope, 0, sizeof(FFENVELOPE));
    memset(&DiCustomForce, 0, sizeof(FFCUSTOMFORCE));
    memset(&DiConstantForce, 0, sizeof(FFCONSTANTFORCE));
    memset(&DiPeriodic, 0, sizeof(FFPERIODIC));
    memset(&DiRampforce, 0, sizeof(FFRAMPFORCE));
    Status  = 0;
    PlayCount = 0;
    StartTime = 0;
    Index = 0;
    LastTime = 0;
}

//----------------------------------------------------------------------------------------------
// Calc
//----------------------------------------------------------------------------------------------
int Feedback360Effect::Calc(LONG *LeftLevel, LONG *RightLevel)
{
    CFTimeInterval Duration = NULL;
    if(DiEffect.dwDuration != FF_INFINITE) {
        Duration = MAX( 1, DiEffect.dwDuration / 1000 ) / 1000;
    } else {
        Duration = DBL_MAX;
    }
    CFAbsoluteTime BeginTime = StartTime + ( DiEffect.dwStartDelay / (1000*1000) );
    CFAbsoluteTime EndTime  = DBL_MAX;
    if( PlayCount != -1 )
    {
        EndTime = BeginTime + Duration * PlayCount;
    }
    CFAbsoluteTime CurrentTime = CFAbsoluteTimeGetCurrent();

    if( Status == FFEGES_PLAYING && BeginTime <= CurrentTime && CurrentTime <= EndTime )
    {
        // Used for force calculation
        LONG NormalLevel;
        LONG WorkLeftLevel;
        LONG WorkRightLevel;

        // Used for envelope calculation
        LONG NormalRate;
        LONG AttackLevel;
        LONG FadeLevel;

        CalcEnvelope(
                     (ULONG)(Duration*1000)
                     ,(ULONG)(fmod(CurrentTime - BeginTime, Duration)*1000)
                     ,&NormalRate
                     ,&AttackLevel
                     ,&FadeLevel );

        // CustomForce allows setting each channel separately
        if(CFEqual(Type, kFFEffectType_CustomForce_ID)) {
            if((CFAbsoluteTimeGetCurrent() - LastTime)*1000*1000 < DiCustomForce.dwSamplePeriod) {
                return -1;
            }
            else {
                WorkLeftLevel = ((DiCustomForce.rglForceData[2*Index] * NormalRate + AttackLevel + FadeLevel) / 100) * DiEffect.dwGain / 10000;
                WorkRightLevel = ((DiCustomForce.rglForceData[2*Index + 1] * NormalRate + AttackLevel + FadeLevel) / 100) * DiEffect.dwGain / 10000;
                Index = (Index + 1) % DiCustomForce.cSamples;
                LastTime = CFAbsoluteTimeGetCurrent();
            }
        }
        // Regular commands treat controller as a single output (both channels are together as one)
        else {
            CalcForce(
                      (ULONG)(Duration*1000)
                      ,(ULONG)(fmod(CurrentTime - BeginTime, Duration)*1000)
                      ,NormalRate
                      ,AttackLevel
                      ,FadeLevel
                      ,&NormalLevel );
            //fprintf(stderr, "DeltaT %f\n", CurrentTime - BeginTime);
            //fprintf(stderr, "Duration %f; NormalRate: %d; AttackLevel: %d; FadeLevel: %d\n", Duration, NormalRate, AttackLevel, FadeLevel);

            fprintf(stderr, "NL: %d\n", NormalLevel);
            WorkLeftLevel = (NormalLevel > 0) ? NormalLevel : -NormalLevel;
            WorkRightLevel = (NormalLevel > 0) ? NormalLevel : -NormalLevel;
        }
        WorkLeftLevel = MIN( SCALE_MAX, WorkLeftLevel * SCALE_MAX / 10000 );
        WorkRightLevel = MIN( SCALE_MAX, WorkRightLevel * SCALE_MAX / 10000 );

        *LeftLevel = *LeftLevel + WorkLeftLevel;
        *RightLevel = *RightLevel + WorkRightLevel;
    }
    return 0;
}

//----------------------------------------------------------------------------------------------
// CalcEnvelope
//----------------------------------------------------------------------------------------------
void Feedback360Effect::CalcEnvelope(ULONG Duration, ULONG CurrentPos, LONG *NormalRate, LONG *AttackLevel, LONG *FadeLevel)
{
	if( ( DiEffect.dwFlags & FFEP_ENVELOPE ) && DiEffect.lpEnvelope != NULL )
	{
        // Calculate attack factor
		LONG	AttackRate	= 0;
		ULONG	AttackTime	= MAX( 1, DiEnvelope.dwAttackTime / 1000 );
		if( CurrentPos < AttackTime )
		{
			AttackRate	= ( AttackTime - CurrentPos ) * 100 / AttackTime;
		}

        // Calculate fade factor
        LONG	FadeRate	= 0;
		ULONG	FadeTime	= MAX( 1, DiEnvelope.dwFadeTime / 1000 );
		ULONG	FadePos		= Duration - FadeTime;
		if( FadePos < CurrentPos )
		{
			FadeRate	= ( CurrentPos - FadePos ) * 100 / FadeTime;
		}

		*NormalRate		= 100 - AttackRate - FadeRate;
		*AttackLevel	= DiEnvelope.dwAttackLevel * AttackRate;
		*FadeLevel		= DiEnvelope.dwFadeLevel * FadeRate;
	} else {
		*NormalRate		= 100;
		*AttackLevel	= 0;
		*FadeLevel		= 0;
	}
}

void Feedback360Effect::CalcForce(ULONG Duration, ULONG CurrentPos, LONG NormalRate, LONG AttackLevel, LONG FadeLevel, LONG * NormalLevel)
{

    LONG Magnitude = 0;
    LONG Period;
    LONG R;
    LONG Rate;

    if(CFEqual(Type, kFFEffectType_ConstantForce_ID)) {
        Magnitude	= DiConstantForce.lMagnitude;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;
    }

    else if(CFEqual(Type, kFFEffectType_Square_ID)) {

        Period	= MAX( 1, ( DiPeriodic.dwPeriod / 1000 ) );
        R		= ( CurrentPos%Period) * 360 / Period;
        R	= ( R + ( DiPeriodic.dwPhase / 100 ) ) % 360;

        Magnitude	= DiPeriodic.dwMagnitude;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;

        if( 180 <= R )
        {
            Magnitude	= Magnitude * -1;
        }

        Magnitude	= Magnitude + DiPeriodic.lOffset;
    }

    else if(CFEqual(Type, kFFEffectType_Sine_ID)) {

        Period	= MAX( 1, ( DiPeriodic.dwPeriod / 1000 ) );
        R		= (CurrentPos%Period) * 360 / Period;
        R		= ( R + ( DiPeriodic.dwPhase / 100 ) ) % 360;

        Magnitude	= DiPeriodic.dwMagnitude;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;

        Magnitude	= ( int)( Magnitude * sin( R * M_PI / 180.0 ) );

        Magnitude	= Magnitude + DiPeriodic.lOffset;
    }

    else if(CFEqual(Type, kFFEffectType_Triangle_ID)) {

        Period	= MAX( 1, ( DiPeriodic.dwPeriod / 1000 ) );
        R		= (CurrentPos%Period) * 360 / Period;
        R		= ( R + ( DiPeriodic.dwPhase / 100 ) ) % 360;

        Magnitude	= DiPeriodic.dwMagnitude;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;

        if( 0 <= R && R < 90 )
        {
            Magnitude	= -Magnitude * ( 90 - R ) / 90;
        }
        if( 90 <= R && R < 180 )
        {
            Magnitude	= Magnitude * ( R - 90 ) / 90;
        }
        if( 180 <= R && R < 270 )
        {
            Magnitude	= Magnitude * ( 90 - ( R - 180 ) ) / 90;
        }
        if( 270 <= R && R < 360 )
        {
            Magnitude	= -Magnitude * ( R - 270 ) / 90;
        }

        Magnitude	= Magnitude + DiPeriodic.lOffset;
    }
    else if(CFEqual(Type, kFFEffectType_SawtoothUp_ID)) {
        Period	= MAX( 1, ( DiPeriodic.dwPeriod / 1000 ) );
        R		= (CurrentPos%Period) * 360 / Period;
        R		= ( R + ( DiPeriodic.dwPhase / 100 ) ) % 360;

        Magnitude	= DiPeriodic.dwMagnitude;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;

        if( 0 <= R && R < 180 )
        {
            Magnitude	= -Magnitude * ( 180 - R ) / 180;
        }
        if( 180 <= R && R < 360 )
        {
            Magnitude	= Magnitude * ( R - 180 ) / 180;
        }

        Magnitude	= Magnitude + DiPeriodic.lOffset;
    }
    else if(CFEqual(Type, kFFEffectType_SawtoothDown_ID)) {
        Period	= MAX( 1, ( DiPeriodic.dwPeriod / 1000 ) );
        R		= (CurrentPos%Period) * 360 / Period;
        R		= ( R + ( DiPeriodic.dwPhase / 100 ) ) % 360;

        Magnitude	= DiPeriodic.dwMagnitude;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;
        if( 0 <= R && R < 180 )
        {
            Magnitude	= Magnitude * ( 180 - R ) / 180;
        }
        if( 180 <= R && R < 360 )
        {
            Magnitude	= -Magnitude * ( R - 180 ) / 180;
        }

        Magnitude	= Magnitude + DiPeriodic.lOffset;
    }

    else if(CFEqual(Type, kFFEffectType_RampForce_ID)) {

        Rate		= ( Duration - CurrentPos ) * 100
        / Duration;//MAX( 1, DiEffect.dwDuration / 1000 );

        Magnitude	= ( DiRampforce.lStart * Rate
                       + DiRampforce.lEnd * ( 100 - Rate ) ) / 100;
        Magnitude	= ( Magnitude * NormalRate + AttackLevel + FadeLevel ) / 100;
    }

    *NormalLevel = Magnitude * (LONG)DiEffect.dwGain / 10000;
}