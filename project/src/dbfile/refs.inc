const DbFileFILE_t DbFileFILE[N_DB_FILE_FILE + 1] = { //   0
	{ 0, 260, 0x00830, 0, FF_UNKNOWN, DBT_NONE, "AIParam/AIParam.baa" },
	{ 2, 260, 0x00830, 0, FF_UNKNOWN, DBT_NONE, "AIParam/AIParam.bas" },
	{ 4, 1, 0x20068, 0, FF_BRRES, DBT_BRRES, "BGteresaSFC.brres" },
	{ 7, 2, 0x20068, 0, FF_BRRES, DBT_BRRES, "BeltCrossing.brres" },
	{ 10, 3, 0x20068, 0, FF_BRRES, DBT_BRRES, "BeltCurveA.brres" },
	{ 13, 5, 0x20068, 0, FF_BRRES, DBT_BRRES, "BeltEasy.brres" },
	{ 16, 7, 0x20068, 0, FF_BRRES, DBT_BRRES, "Block.brres" },
	{ 19, 9, 0x20068, 0, FF_BRRES, DBT_BRRES, "CarA1.brres" },
	{ 22, 10, 0x20068, 0, FF_BRRES, DBT_BRRES, "CarA2.brres" },
	{ 25, 11, 0x20068, 0, FF_BRRES, DBT_BRRES, "CarA3.brres" },
	//  10
	{ 28, 12, 0x20068, 0, FF_BRRES, DBT_BRRES, "CarB.brres" },
	{ 31, 13, 0x20068, 0, FF_BRRES, DBT_BRRES, "Crane.brres" },
	{ 34, 13, 0x20228, 0, FF_KCL, DBT_KCL, "Crane.kcl" },
	{ 37, 14, 0x20068, 0, FF_BRRES, DBT_BRRES, "Crescent64.brres" },
	{ 40, 17, 0x20068, 0, FF_BRRES, DBT_BRRES, "DKrockGC.brres" },
	{ 44, 19, 0x20068, 0, FF_BRRES, DBT_BRRES, "DKship64.brres" },
	{ 47, 20, 0x26068, 22, FF_BRRES, DBT_BRRES, "DKtreeA64.brres" },
	{ 50, 21, 0x26068, 31, FF_BRRES, DBT_BRRES, "DKtreeB64.brres" },
	{ 53, 22, 0x2e068, 40, FF_BRRES, DBT_BRRES, "DKturibashiGC.brres" },
	{ 56, 23, 0x20068, 0, FF_BRRES, DBT_BRRES, "DemoCol.brres" },
	//  20
	{ 59, 23, 0x20228, 0, FF_KCL, DBT_KCL, "DemoCol.kcl" },
	{ 62, 25, 0x20068, 0, FF_BRRES, DBT_BRRES, "DemoJugemu.brres" },
	{ 65, 26, 0x20068, 0, FF_BRRES, DBT_BRRES, "DonkyCannonGC.brres" },
	{ 68, 27, 0x20068, 0, FF_BRRES, DBT_BRRES, "DonkyCannon_wii.brres" },
	{ 71, 28, 0x20068, 0, FF_BRRES, DBT_BRRES, "EarthRing.brres" },
	{ 74, 33, 0x20068, 0, FF_BRRES, DBT_BRRES, "Epropeller.brres" },
	{ 77, 39, 0x20068, 0, FF_BRRES, DBT_BRRES, "FireSnake.brres" },
	{ 82, 40, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagA1.brres" },
	{ 85, 41, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagA2.brres" },
	{ 88, 42, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagA3.brres" },
	//  30
	{ 91, 44, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagA5.brres" },
	{ 94, 45, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagB1.brres" },
	{ 97, 46, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagB2.brres" },
	{ 102, 47, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagB3.brres" },
	{ 105, 48, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlagB4.brres" },
	{ 108, 49, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlamePole.brres" },
	{ 111, 49, 0x20228, 0, FF_KCL, DBT_KCL, "FlamePole.kcl" },
	{ 114, 51, 0x20068, 0, FF_BRRES, DBT_BRRES, "FlamePole_v.brres" },
	{ 121, 60, 0x20068, 0, FF_BRRES, DBT_BRRES, "HeyhoBallGBA.brres" },
	{ 124, 61, 0x20068, 0, FF_BRRES, DBT_BRRES, "HeyhoShipGBA.brres" },
	//  40
	{ 127, 62, 0x26068, 82, FF_BRRES, DBT_BRRES, "HeyhoTreeGBA.brres" },
	{ 130, 63, 0x20068, 0, FF_BRRES, DBT_BRRES, "InsekiA.brres" },
	{ 133, 64, 0x20068, 0, FF_BRRES, DBT_BRRES, "InsekiB.brres" },
	{ 136, 65, 0x20068, 0, FF_BRRES, DBT_BRRES, "K_bomb_car.brres" },
	{ 140, 66, 0x50068, 0, FF_BRRES, DBT_BRRES, "K_bomb_car1.brres" },
	{ 142, 68, 0x20068, 0, FF_BRRES, DBT_BRRES, "K_car_body.brres" },
	{ 145, 69, 0x20068, 0, FF_BRRES, DBT_BRRES, "K_chairlift00.brres" },
	{ 148, 70, 0x20068, 0, FF_BRRES, DBT_BRRES, "K_sticklift00.brres" },
	{ 151, 71, 0x20068, 0, FF_BRRES, DBT_BRRES, "K_truck.brres" },
	{ 154, 73, 0x20068, 0, FF_BRRES, DBT_BRRES, "KmoonZ.brres" },
	//  50
	{ 157, 74, 0x20068, 0, FF_BRRES, DBT_BRRES, "KoopaFigure64.brres" },
	{ 160, 75, 0x20068, 0, FF_BRRES, DBT_BRRES, "Ksticketc.brres" },
	{ 163, 77, 0x50068, 0, FF_BRRES, DBT_BRRES, "M_obj_jump.brres" },
	{ 166, 77, 0x50228, 0, FF_KCL, DBT_KCL, "M_obj_jump.kcl" },
	{ 169, 78, 0x50068, 0, FF_BRRES, DBT_BRRES, "M_obj_jump2.brres" },
	{ 171, 78, 0x50228, 0, FF_KCL, DBT_KCL, "M_obj_jump2.kcl" },
	{ 173, 81, 0x50068, 0, FF_BRRES, DBT_BRRES, "M_obj_s_jump.brres" },
	{ 176, 81, 0x50228, 0, FF_KCL, DBT_KCL, "M_obj_s_jump.kcl" },
	{ 179, 82, 0x50068, 0, FF_BRRES, DBT_BRRES, "M_obj_s_jump2.brres" },
	{ 181, 82, 0x50228, 0, FF_KCL, DBT_KCL, "M_obj_s_jump2.kcl" },
	//  60
	{ 183, 84, 0x20068, 0, FF_BRRES, DBT_BRRES, "MarioGo64.brres" },
	{ 186, 85, 0x20068, 0, FF_BRRES, DBT_BRRES, "MashBalloonGC.brres" },
	{ 191, 86, 0x20068, 0, FF_BRRES, DBT_BRRES, "Mdush.brres" },
	{ 194, 86, 0x20228, 0, FF_KCL, DBT_KCL, "Mdush.kcl" },
	{ 197, 87, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiKanban.brres" },
	{ 202, 88, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiObj01.brres" },
	{ 207, 89, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiObj02.brres" },
	{ 212, 90, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiObj03.brres" },
	{ 217, 91, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiObjD01.brres" },
	{ 220, 92, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiObjD02.brres" },
	//  70
	{ 223, 93, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiObjD03.brres" },
	{ 226, 94, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiSighKino.brres" },
	{ 229, 95, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiSignNoko.brres" },
	{ 232, 96, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiSignWario.brres" },
	{ 235, 98, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiSphinxY2.brres" },
	{ 238, 100, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueBD1.brres" },
	{ 241, 101, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueBL1.brres" },
	{ 244, 102, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueD1.brres" },
	{ 247, 103, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueD2.brres" },
	{ 250, 105, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueL1.brres" },
	//  80
	{ 253, 106, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueL2.brres" },
	{ 256, 107, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueL3.brres" },
	{ 261, 108, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueM1.brres" },
	{ 264, 109, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueM2.brres" },
	{ 267, 110, 0x20068, 0, FF_BRRES, DBT_BRRES, "MiiStatueP1.brres" },
	{ 270, 112, 0x26068, 126, FF_BRRES, DBT_BRRES, "PalmTree.brres" },
	{ 273, 113, 0x20068, 0, FF_BRRES, DBT_BRRES, "PeachHunsuiGC.brres" },
	{ 276, 114, 0x20068, 0, FF_BRRES, DBT_BRRES, "Piston.brres" },
	{ 279, 115, 0x20068, 0, FF_BRRES, DBT_BRRES, "Press.brres" },
	{ 282, 116, 0x20068, 0, FF_BRRES, DBT_BRRES, "Psea.brres" },
	//  90
	{ 285, 117, 0x20068, 0, FF_BRRES, DBT_BRRES, "RM_ring1.brres" },
	{ 288, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring1.kcl" },
	{ 291, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring1b.kcl" },
	{ 294, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring2.kcl" },
	{ 297, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring2b.kcl" },
	{ 300, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring2c.kcl" },
	{ 303, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring3.kcl" },
	{ 306, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring3b.kcl" },
	{ 309, 117, 0x2022c, 0, FF_KCL, DBT_KCL, "RM_ring3c.kcl" },
	{ 312, 118, 0x20068, 0, FF_BRRES, DBT_BRRES, "RhMiiKanban.brres" },
	// 100
	{ 315, 119, 0x20068, 0, FF_BRRES, DBT_BRRES, "SentakuDS.brres" },
	{ 318, 120, 0x20068, 0, FF_BRRES, DBT_BRRES, "ShMiiObj01.brres" },
	{ 321, 121, 0x20068, 0, FF_BRRES, DBT_BRRES, "ShMiiObj02.brres" },
	{ 324, 122, 0x20068, 0, FF_BRRES, DBT_BRRES, "ShMiiObj03.brres" },
	{ 327, 123, 0x20068, 0, FF_BRRES, DBT_BRRES, "SpaceSun.brres" },
	{ 330, 124, 0x20068, 0, FF_BRRES, DBT_BRRES, "Spot64.brres" },
	{ 333, 125, 0x20068, 0, FF_BRRES, DBT_BRRES, "StarRing.brres" },
	{ 336, 127, 0x20068, 0, FF_BRRES, DBT_BRRES, "TownBridgeDS.brres" },
	{ 339, 127, 0x20228, 0, FF_KCL, DBT_KCL, "TownBridgeDS.kcl" },
	{ 342, 127, 0x20228, 0, FF_KCL, DBT_KCL, "TownBridgeDS2.kcl" },
	// 110
	{ 345, 127, 0x20228, 0, FF_KCL, DBT_KCL, "TownBridgeDS3.kcl" },
	{ 348, 128, 0x26068, 144, FF_BRRES, DBT_BRRES, "TownTreeDS.brres" },
	{ 351, 129, 0x20068, 0, FF_BRRES, DBT_BRRES, "TruckWagon.brres" },
	{ 354, 130, 0x20068, 0, FF_BRRES, DBT_BRRES, "Twanwan.brres" },
	{ 358, 132, 0x20068, 0, FF_BRRES, DBT_BRRES, "TwistedWay.brres" },
	{ 361, 133, 0x20068, 0, FF_BRRES, DBT_BRRES, "UtsuboDokan.brres" },
	{ 364, 134, 0x20068, 0, FF_BRRES, DBT_BRRES, "VolcanoBall1.brres" },
	{ 367, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece0.kcl" },
	{ 370, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece0b.kcl" },
	{ 373, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece0c.kcl" },
	// 120
	{ 376, 135, 0x20068, 0, FF_BRRES, DBT_BRRES, "VolcanoPiece1.brres" },
	{ 379, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece1.kcl" },
	{ 382, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece11.kcl" },
	{ 385, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece11b.kcl" },
	{ 388, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece11c.kcl" },
	{ 391, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece13.kcl" },
	{ 394, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece13b.kcl" },
	{ 397, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece13c.kcl" },
	{ 400, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece14.kcl" },
	{ 403, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece14b.kcl" },
	// 130
	{ 406, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece14c.kcl" },
	{ 409, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece16.kcl" },
	{ 412, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece16b.kcl" },
	{ 415, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece16c.kcl" },
	{ 418, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece18.kcl" },
	{ 421, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece18b.kcl" },
	{ 424, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece18c.kcl" },
	{ 427, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece1b.kcl" },
	{ 430, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece1c.kcl" },
	{ 433, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece2.kcl" },
	// 140
	{ 436, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece2b.kcl" },
	{ 439, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece2c.kcl" },
	{ 442, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece3.kcl" },
	{ 445, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece3b.kcl" },
	{ 448, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece3c.kcl" },
	{ 451, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece4.kcl" },
	{ 454, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece4b.kcl" },
	{ 457, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece4c.kcl" },
	{ 460, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece5.kcl" },
	{ 463, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece5b.kcl" },
	// 150
	{ 466, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece5c.kcl" },
	{ 469, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece7.kcl" },
	{ 472, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece7b.kcl" },
	{ 475, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece7c.kcl" },
	{ 478, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece8.kcl" },
	{ 481, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece8b.kcl" },
	{ 484, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece8c.kcl" },
	{ 487, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece9.kcl" },
	{ 490, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece9b.kcl" },
	{ 493, 135, 0x2022c, 0, FF_KCL, DBT_KCL, "VolcanoPiece9c.kcl" },
	// 160
	{ 496, 136, 0x20068, 0, FF_BRRES, DBT_BRRES, "VolcanoRock1.brres" },
	{ 499, 136, 0x20228, 0, FF_KCL, DBT_KCL, "VolcanoRock1.kcl" },
	{ 502, 136, 0x20228, 0, FF_KCL, DBT_KCL, "VolcanoRock2.kcl" },
	{ 505, 137, 0x20068, 0, FF_BRRES, DBT_BRRES, "WLarrowGC.brres" },
	{ 508, 138, 0x26068, 153, FF_BRRES, DBT_BRRES, "WLdokanGC.brres" },
	{ 511, 138, 0x20228, 0, FF_KCL, DBT_KCL, "WLdokanGC.kcl" },
	{ 514, 139, 0x20068, 0, FF_BRRES, DBT_BRRES, "WLfirebarGC.brres" },
	{ 519, 140, 0x26068, 159, FF_BRRES, DBT_BRRES, "WLscreenGC.brres" },
	{ 522, 141, 0x20068, 0, FF_BRRES, DBT_BRRES, "WLwallGC.brres" },
	{ 525, 141, 0x20228, 0, FF_KCL, DBT_KCL, "WLwallGC.kcl" },
	// 170
	{ 528, 142, 0x20068, 0, FF_BRRES, DBT_BRRES, "ami.brres" },
	{ 531, 143, 0x20068, 0, FF_BRRES, DBT_BRRES, "aurora.brres" },
	{ 534, 144, 0x20068, 0, FF_BRRES, DBT_BRRES, "b_teresa.brres" },
	{ 537, 145, 0x20068, 0, FF_BRRES, DBT_BRRES, "basabasa.brres" },
	{ 543, 146, 0x20068, 0, FF_BRRES, DBT_BRRES, "bblock1.brres" },
	{ 546, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock1.kcl" },
	{ 549, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock1b.kcl" },
	{ 552, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock2.kcl" },
	{ 555, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock2b.kcl" },
	{ 558, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock3.kcl" },
	// 180
	{ 561, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock3b.kcl" },
	{ 564, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock4.kcl" },
	{ 567, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock4b.kcl" },
	{ 570, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock5.kcl" },
	{ 573, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock5b.kcl" },
	{ 576, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock6.kcl" },
	{ 579, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock6b.kcl" },
	{ 582, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock7.kcl" },
	{ 585, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock7b.kcl" },
	{ 588, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock8.kcl" },
	// 190
	{ 591, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock8b.kcl" },
	{ 594, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock9.kcl" },
	{ 597, 146, 0x2022c, 0, FF_KCL, DBT_KCL, "bblock9b.kcl" },
	{ 600, 147, 0x20068, 0, FF_BRRES, DBT_BRRES, "begoman_spike.brres" },
	{ 603, 148, 0x20068, 0, FF_BRRES, DBT_BRRES, "bird.brres" },
	{ 606, 149, 0x20068, 0, FF_BRRES, DBT_BRRES, "boble.brres" },
	{ 609, 26, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/DonkyCannonGC/DonkyCannonGC.brasd" },
	{ 612, 27, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/DonkyCannon_wii/DonkyCannon_wii.brasd" },
	{ 615, 160, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/cow/eat.brasd" },
	{ 618, 160, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/cow/eat_ed.brasd" },
	// 200
	{ 621, 160, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/cow/run.brasd" },
	{ 624, 160, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/cow/surprise.brasd" },
	{ 627, 160, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/cow/walk.brasd" },
	{ 630, 161, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/crab/wait.brasd" },
	{ 633, 161, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/crab/walk_l.brasd" },
	{ 636, 161, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/crab/walk_r.brasd" },
	{ 639, 187, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/hanachan/head_wait2.brasd" },
	{ 643, 187, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/hanachan/walk.brasd" },
	{ 647, 199, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/koopaFigure/vomit.brasd" },
	{ 650, 199, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/koopaFigure/wait.brasd" },
	// 210
	{ 653, 201, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/kuribo/walk_l.brasd" },
	{ 665, 201, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/kuribo/walk_r.brasd" },
	{ 677, 218, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/pakkun_f/attack.brasd" },
	{ 682, 218, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/pakkun_f/wait.brasd" },
	{ 687, 222, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/penguin_m/walk.brasd" },
	{ 690, 223, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/penguin_s/dive.brasd" },
	{ 693, 223, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/penguin_s/stand_up.brasd" },
	{ 696, 223, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/penguin_s/walk.brasd" },
	{ 699, 228, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/poihana/throw.brasd" },
	{ 703, 228, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/poihana/walk.brasd" },
	// 220
	{ 707, 229, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/puchi_pakkun/puchi_pakkun.brasd" },
	{ 710, 235, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/sanbo/revive.brasd" },
	{ 716, 243, 0x20128, 0, FF_BRASD, DBT_BRASD, "brasd/tree_cannon/tree_cannon.brasd" },
	{ 719, 150, 0x20068, 0, FF_BRRES, DBT_BRRES, "bulldozer_left.brres" },
	{ 722, 150, 0x20228, 0, FF_KCL, DBT_KCL, "bulldozer_left.kcl" },
	{ 725, 151, 0x20068, 0, FF_BRRES, DBT_BRRES, "bulldozer_right.brres" },
	{ 728, 151, 0x20228, 0, FF_KCL, DBT_KCL, "bulldozer_right.kcl" },
	{ 731, 153, 0x20068, 0, FF_BRRES, DBT_BRRES, "casino_roulette.brres" },
	{ 734, 153, 0x20228, 0, FF_KCL, DBT_KCL, "casino_roulette.kcl" },
	{ 737, 154, 0x20068, 0, FF_BRRES, DBT_BRRES, "castleballoon1.brres" },
	// 230
	{ 742, 155, 0x20048, 0, FF_BRRES, DBT_BRRES, "castleflower1.brres" },
	{ 751, 156, 0x26048, 1, FF_BRRES, DBT_BRRES, "castletree1.brres" },
	{ 762, 157, 0x26048, 10, FF_BRRES, DBT_BRRES, "castletree2.brres" },
	{ 773, 158, 0x20068, 0, FF_BRRES, DBT_BRRES, "choropu.brres" },
	{ 778, 159, 0x26068, 19, FF_BRRES, DBT_BRRES, "coin.brres" },
	{ 800, 259, 0x20820, 0, FF_UNKNOWN, DBT_NONE, "course.0" },
	{ 803, 257, 0x00800, 0, FF_UNKNOWN, DBT_NONE, "course.bmm" },
	{ 816, 257, 0x00800, 0, FF_UNKNOWN, DBT_NONE, "course.btiEnv" },
	{ 829, 257, 0x00800, 0, FF_UNKNOWN, DBT_NONE, "course.btiMat" },
	{ 842, 257, 0x00201, 0, FF_KCL, DBT_KCL, "course.kcl" },
	// 240
	{ 942, 257, 0x00401, 0, FF_KMP, DBT_NONE, "course.kmp" },
	{ 1042, 260, 0x00834, 0, FF_UNKNOWN, DBT_NONE, "course.lex" },
	{ 1044, 257, 0x00042, 0, FF_BRRES, DBT_BRRES, "course_d_model.brres" },
	{ 1065, 257, 0x00042, 0, FF_BRRES, DBT_BRRES, "course_model.brres" },
	{ 1145, 160, 0x20068, 0, FF_BRRES, DBT_BRRES, "cow.brres" },
	{ 1148, 161, 0x20068, 0, FF_BRRES, DBT_BRRES, "crab.brres" },
	{ 1151, 162, 0x20068, 0, FF_BRRES, DBT_BRRES, "cruiser.brres" },
	{ 1156, 163, 0x20068, 0, FF_BRRES, DBT_BRRES, "dc_pillar.brres" },
	{ 1159, 163, 0x20228, 0, FF_KCL, DBT_KCL, "dc_pillar.kcl" },
	{ 1162, 260, 0x20220, 0, FF_KCL, DBT_KCL, "dc_pillar_base.kcl" },
	// 250
	{ 1165, 164, 0x20068, 0, FF_BRRES, DBT_BRRES, "dc_sandcone.brres" },
	{ 1168, 164, 0x20228, 0, FF_KCL, DBT_KCL, "dc_sandcone.kcl" },
	{ 1171, 165, 0x20068, 0, FF_BRRES, DBT_BRRES, "dk_miiobj00.brres" },
	{ 1174, 166, 0x20068, 0, FF_BRRES, DBT_BRRES, "dkmonitor.brres" },
	{ 1177, 167, 0x26068, 49, FF_BRRES, DBT_BRRES, "dokan_sfc.brres" },
	{ 1183, 168, 0x26068, 55, FF_BRRES, DBT_BRRES, "donkytree1GC.brres" },
	{ 1186, 169, 0x26068, 64, FF_BRRES, DBT_BRRES, "donkytree2GC.brres" },
	{ 1189, 170, 0x20068, 0, FF_BRRES, DBT_BRRES, "dossun.brres" },
	{ 1199, 0, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Alarm/rk_alarm.breff" },
	{ 1202, 0, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Alarm/rk_alarm.breft" },
	// 260
	{ 1205, 7, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Block/rk_block.breff" },
	{ 1208, 7, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Block/rk_block.breft" },
	{ 1211, 15, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DKfalls/rk_64DKfalls.breff" },
	{ 1214, 15, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DKfalls/rk_64DKfalls.breft" },
	{ 1217, 17, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DKrockGC/rk_DKrockGC.breff" },
	{ 1221, 17, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DKrockGC/rk_DKrockGC.breft" },
	{ 1225, 19, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DKship64/rk_DKship64.breff" },
	{ 1228, 19, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DKship64/rk_DKship64.breft" },
	{ 1231, 24, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DemoEf/rk_demo.breff" },
	{ 1234, 24, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DemoEf/rk_demo.breft" },
	// 270
	{ 1237, 25, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DemoJugemu/rk_DemoJugemu.breff" },
	{ 1240, 25, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DemoJugemu/rk_DemoJugemu.breft" },
	{ 1243, 26, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DonkyCannonGC/rk_dkCannon.breff" },
	{ 1246, 26, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DonkyCannonGC/rk_dkCannon.breft" },
	{ 1249, 27, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/DonkyCannon_wii/rk_dkCannon.breff" },
	{ 1252, 27, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/DonkyCannon_wii/rk_dkCannon.breft" },
	{ 1255, 29, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/EnvFire/rk_EnvFire.breff" },
	{ 1264, 29, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/EnvFire/rk_EnvFire.breft" },
	{ 1273, 30, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/EnvKareha/rk_EnvKareha.breff" },
	{ 1276, 30, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/EnvKareha/rk_EnvKareha.breft" },
	// 280
	{ 1279, 31, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/EnvKarehaUp/rk_EnvKarehaUp.breff" },
	{ 1282, 31, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/EnvKarehaUp/rk_EnvKarehaUp.breft" },
	{ 1285, 32, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/EnvSnow/rk_EnvSnow.breff" },
	{ 1288, 32, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/EnvSnow/rk_EnvSnow.breft" },
	{ 1291, 33, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Epropeller/rk_epropeller.breff" },
	{ 1294, 33, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Epropeller/rk_epropeller.breft" },
	{ 1297, 34, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/FallBsA/rk_fallbasin.breff" },
	{ 1300, 34, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/FallBsA/rk_fallbasin.breft" },
	{ 1303, 35, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/FallBsB/rk_fallbasin.breff" },
	{ 1306, 35, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/FallBsB/rk_fallbasin.breft" },
	// 290
	{ 1309, 37, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Fall_MH/rk_ridgeFall.breff" },
	{ 1312, 37, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Fall_MH/rk_ridgeFall.breft" },
	{ 1315, 38, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Fall_Y/rk_yoshiFalls.breff" },
	{ 1318, 38, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Fall_Y/rk_yoshiFalls.breft" },
	{ 1321, 39, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/FireSnake/rk_fireSnake.breff" },
	{ 1326, 39, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/FireSnake/rk_fireSnake.breft" },
	{ 1331, 49, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/FlamePole/rk_flamePole.breff" },
	{ 1334, 49, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/FlamePole/rk_flamePole.breft" },
	{ 1337, 51, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/FlamePole_v/rk_flamePole.breff" },
	{ 1344, 51, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/FlamePole_v/rk_flamePole.breft" },
	// 300
	{ 1351, 52, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/FlamePole_v_big/rk_flamePoleL.breff" },
	{ 1354, 52, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/FlamePole_v_big/rk_flamePoleL.breft" },
	{ 1357, 53, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Flash_B/rk_flashBCross.breff" },
	{ 1360, 53, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Flash_B/rk_flashBCross.breft" },
	{ 1363, 54, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Flash_L/rk_flashBeginner.breff" },
	{ 1366, 54, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Flash_L/rk_flashBeginner.breft" },
	{ 1369, 55, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Flash_M/rk_flash64mario.breff" },
	{ 1372, 55, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Flash_M/rk_flash64mario.breft" },
	{ 1375, 56, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Flash_S/rk_flashSkate.breff" },
	{ 1378, 56, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Flash_S/rk_flashSkate.breft" },
	// 310
	{ 1381, 57, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Flash_W/rk_flashWaluigi.breff" },
	{ 1384, 57, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Flash_W/rk_flashWaluigi.breft" },
	{ 1387, 59, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Hanabi/rk_stHanabi.breff" },
	{ 1404, 59, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Hanabi/rk_stHanabi.breft" },
	{ 1421, 60, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/HeyhoBallGBA/rk_HeyhoBallGBA.breff" },
	{ 1424, 60, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/HeyhoBallGBA/rk_HeyhoBallGBA.breft" },
	{ 1427, 65, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/K_bomb_car/rk_K_bomb_car.breff" },
	{ 1431, 65, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/K_bomb_car/rk_K_bomb_car.breft" },
	{ 1435, 66, 0x500a8, 0, FF_BREFF, DBT_BREFF, "effect/K_bomb_car1/rk_K_bomb_car.breff" },
	{ 1437, 66, 0x500a8, 0, FF_BREFT, DBT_BREFT, "effect/K_bomb_car1/rk_K_bomb_car.breft" },
	// 320
	{ 1439, 72, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Kamifubuki/rk_kamifubuki.breff" },
	{ 1442, 72, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Kamifubuki/rk_kamifubuki.breft" },
	{ 1445, 74, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/KoopaFigure64/rk_koopaFire.breff" },
	{ 1448, 74, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/KoopaFigure64/rk_koopaFire.breft" },
	{ 1451, 113, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/PeachHunsuiGC/rk_hunsui.breff" },
	{ 1454, 113, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/PeachHunsuiGC/rk_hunsui.breft" },
	{ 1457, 115, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Press/rk_press.breff" },
	{ 1460, 115, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Press/rk_press.breft" },
	{ 1463, 125, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/StarRing/rk_StarRing.breff" },
	{ 1466, 125, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/StarRing/rk_StarRing.breft" },
	// 330
	{ 1469, 126, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/Steam/rk_steam.breff" },
	{ 1472, 126, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/Steam/rk_steam.breft" },
	{ 1475, 129, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/TruckWagon/rk_truck.breff" },
	{ 1478, 129, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/TruckWagon/rk_truck.breft" },
	{ 1481, 134, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/VolcanoBall1/rk_volc.breff" },
	{ 1484, 134, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/VolcanoBall1/rk_volc.breft" },
	{ 1487, 147, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/begoman_spike/rk_begoman_spike.breff" },
	{ 1490, 147, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/begoman_spike/rk_begoman_spike.breft" },
	{ 1493, 149, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/boble/rk_boble.breff" },
	{ 1496, 149, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/boble/rk_boble.breft" },
	// 340
	{ 1499, 158, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/choropu/rk_choropuMoko.breff" },
	{ 1504, 158, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/choropu/rk_choropuMoko.breft" },
	{ 1509, 159, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/coin/rk_coin.breff" },
	{ 1531, 159, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/coin/rk_coin.breft" },
	{ 1553, 161, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/crab/rk_crab.breff" },
	{ 1556, 161, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/crab/rk_crab.breft" },
	{ 1559, 162, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/cruiser/rk_cruiser.breff" },
	{ 1564, 162, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/cruiser/rk_cruiser.breft" },
	{ 1569, 163, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/dc_pillar/rk_dc_pillar.breff" },
	{ 1572, 163, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/dc_pillar/rk_dc_pillar.breft" },
	// 350
	{ 1575, 164, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/dc_sandcone/rk_dc_sandcone.breff" },
	{ 1578, 164, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/dc_sandcone/rk_dc_sandcone.breft" },
	{ 1581, 170, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/dossun/rk_dossun.breff" },
	{ 1591, 170, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/dossun/rk_dossun.breft" },
	{ 1601, 172, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/entry/rk_entry.breff" },
	{ 1606, 172, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/entry/rk_entry.breft" },
	{ 1611, 187, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/hanachan/rk_hana.breff" },
	{ 1615, 187, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/hanachan/rk_hana.breft" },
	{ 1619, 190, 0x500a8, 0, FF_BREFF, DBT_BREFF, "effect/honeBall/rk_honeBall.breff" },
	{ 1621, 190, 0x500a8, 0, FF_BREFT, DBT_BREFT, "effect/honeBall/rk_honeBall.breft" },
	// 360
	{ 1623, 193, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/karehayama/rk_karehayama.breff" },
	{ 1626, 193, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/karehayama/rk_karehayama.breft" },
	{ 1629, 198, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/koopaBall/rk_koopaBall.breff" },
	{ 1632, 198, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/koopaBall/rk_koopaBall.breft" },
	{ 1635, 202, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/leaf_effect/rk_leaf_effect.breff" },
	{ 1638, 202, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/leaf_effect/rk_leaf_effect.breft" },
	{ 1641, 221, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/penguin_l/rk_penguin_l.breff" },
	{ 1644, 221, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/penguin_l/rk_penguin_l.breft" },
	{ 1647, 222, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/penguin_m/rk_penguin_m.breff" },
	{ 1650, 222, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/penguin_m/rk_penguin_m.breft" },
	// 370
	{ 1653, 223, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/penguin_s/rk_penguin_s.breff" },
	{ 1656, 223, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/penguin_s/rk_penguin_s.breft" },
	{ 1659, 225, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/pocha/rk_pocha.breff" },
	{ 1690, 225, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/pocha/rk_pocha.breft" },
	{ 1721, 226, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/pochaMori/rk_pochaLeaf.breff" },
	{ 1726, 226, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/pochaMori/rk_pochaLeaf.breft" },
	{ 1731, 227, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/pochaYogan/rk_pochaYogan.breff" },
	{ 1740, 227, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/pochaYogan/rk_pochaYogan.breft" },
	{ 1749, 228, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/poihana/rk_poihana.breff" },
	{ 1753, 228, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/poihana/rk_poihana.breft" },
	// 380
	{ 1757, 232, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/quicksand/rk_qsandDossun.breff" },
	{ 1760, 232, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/quicksand/rk_qsandDossun.breft" },
	{ 1763, 235, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/sanbo/rk_sanbo.breff" },
	{ 1769, 235, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/sanbo/rk_sanbo.breft" },
	{ 1775, 239, 0x500a8, 0, FF_BREFF, DBT_BREFF, "effect/starGate/rk_starGate.breff" },
	{ 1777, 239, 0x500a8, 0, FF_BREFT, DBT_BREFT, "effect/starGate/rk_starGate.breft" },
	{ 1779, 242, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/taimatsu/rk_taimatsu.breff" },
	{ 1788, 242, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/taimatsu/rk_taimatsu.breft" },
	{ 1797, 243, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/tree_cannon/rk_dkCannon.breff" },
	{ 1800, 243, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/tree_cannon/rk_dkCannon.breft" },
	// 390
	{ 1803, 244, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/truckChimSmk/rk_truckChimSmk.breff" },
	{ 1808, 244, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/truckChimSmk/rk_truckChimSmk.breft" },
	{ 1813, 245, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/truckChimSmkW/rk_truckChimSmkW.breff" },
	{ 1816, 245, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/truckChimSmkW/rk_truckChimSmkW.breft" },
	{ 1819, 249, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/venice_hasi/rk_venice.breff" },
	{ 1822, 249, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/venice_hasi/rk_venice.breft" },
	{ 1825, 253, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/volsmk/rk_volsmk.breff" },
	{ 1828, 253, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/volsmk/rk_volsmk.breft" },
	{ 1831, 254, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/wanwan/rk_wanwan.breff" },
	{ 1839, 254, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/wanwan/rk_wanwan.breft" },
	// 400
	{ 1847, 256, 0x200a8, 0, FF_BREFF, DBT_BREFF, "effect/woodbox/rk_woodbox.breff" },
	{ 1853, 256, 0x200a8, 0, FF_BREFT, DBT_BREFT, "effect/woodbox/rk_woodbox.breft" },
	{ 1859, 173, 0x20068, 0, FF_BRRES, DBT_BRRES, "escalator.brres" },
	{ 1862, 173, 0x20228, 0, FF_KCL, DBT_KCL, "escalator.kcl" },
	{ 1865, 174, 0x20068, 0, FF_BRRES, DBT_BRRES, "fks_screen_wii.brres" },
	{ 1868, 177, 0x26068, 73, FF_BRRES, DBT_BRRES, "gardentreeDS.brres" },
	{ 1871, 178, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_enemy_a.brres" },
	{ 1880, 179, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_enemy_a_demo.brres" },
	{ 1883, 180, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_enemy_b.brres" },
	{ 1890, 181, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_enemy_c.brres" },
	// 410
	{ 1897, 183, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_enemy_e.brres" },
	{ 1900, 184, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_enemy_f.brres" },
	{ 1903, 185, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_monte_a.brres" },
	{ 1916, 186, 0x20068, 0, FF_BRRES, DBT_BRRES, "group_monte_a_demo.brres" },
	{ 1919, 187, 0x20068, 0, FF_BRRES, DBT_BRRES, "hanachan.brres" },
	{ 1923, 188, 0x20068, 0, FF_BRRES, DBT_BRRES, "heyho.brres" },
	{ 1926, 189, 0x20068, 0, FF_BRRES, DBT_BRRES, "heyho2.brres" },
	{ 1929, 190, 0x50068, 0, FF_BRRES, DBT_BRRES, "honeBall.brres" },
	{ 1931, 191, 0x2006c, 0, FF_BRRES, DBT_BRRES, "ice.brres" },
	{ 1934, 260, 0x00834, 0, FF_UNKNOWN, DBT_NONE, "info.txt" },
	// 420
	{ 1936, 192, 0x20068, 91, FF_BRRES, DBT_BRRES, "itembox.brres" },
	{ 2025, 193, 0x26068, 94, FF_BRRES, DBT_BRRES, "karehayama.brres" },
	{ 2028, 195, 0x2e068, 102, FF_BRRES, DBT_BRRES, "kinoko.brres" },
	{ 2032, 196, 0x20068, 0, FF_BRRES, DBT_BRRES, "kinokoT1.brres" },
	{ 2035, 195, 0x20228, 0, FF_KCL, DBT_KCL, "kinoko_d_g.kcl" },
	{ 2039, 195, 0x20228, 0, FF_KCL, DBT_KCL, "kinoko_d_r.kcl" },
	{ 2043, 195, 0x20228, 0, FF_KCL, DBT_KCL, "kinoko_g.kcl" },
	{ 2047, 195, 0x20228, 0, FF_KCL, DBT_KCL, "kinoko_r.kcl" },
	{ 2051, 198, 0x20068, 0, FF_BRRES, DBT_BRRES, "koopaBall.brres" },
	{ 2054, 199, 0x20068, 0, FF_BRRES, DBT_BRRES, "koopaFigure.brres" },
	// 430
	{ 2057, 200, 0x20068, 0, FF_BRRES, DBT_BRRES, "koopaFirebar.brres" },
	{ 2060, 201, 0x20068, 0, FF_BRRES, DBT_BRRES, "kuribo.brres" },
	{ 2072, 204, 0x20068, 0, FF_BRRES, DBT_BRRES, "light_house.brres" },
	{ 2075, 257, 0x00041, 0, FF_BRRES, DBT_BRRES, "map_model.brres" },
	{ 2172, 205, 0x20068, 0, FF_BRRES, DBT_BRRES, "mare_a.brres" },
	{ 2175, 206, 0x20068, 0, FF_BRRES, DBT_BRRES, "mare_b.brres" },
	{ 2178, 207, 0x26068, 105, FF_BRRES, DBT_BRRES, "mariotreeGC.brres" },
	{ 2183, 208, 0x20068, 0, FF_BRRES, DBT_BRRES, "mii_balloon.brres" },
	{ 2186, 209, 0x20068, 0, FF_BRRES, DBT_BRRES, "miiposter.brres" },
	{ 2189, 210, 0x20068, 0, FF_BRRES, DBT_BRRES, "monte_a.brres" },
	// 440
	{ 2196, 211, 0x20068, 0, FF_BRRES, DBT_BRRES, "moray.brres" },
	{ 2201, 215, 0x26068, 114, FF_BRRES, DBT_BRRES, "obakeblockSFC.brres" },
	{ 2204, 216, 0x20068, 0, FF_BRRES, DBT_BRRES, "oilSFC.brres" },
	{ 2208, 217, 0x26068, 119, FF_BRRES, DBT_BRRES, "pakkun_dokan.brres" },
	{ 2211, 218, 0x20068, 0, FF_BRRES, DBT_BRRES, "pakkun_f.brres" },
	{ 2216, 219, 0x20068, 0, FF_BRRES, DBT_BRRES, "parasol.brres" },
	{ 2219, 220, 0x26068, 135, FF_BRRES, DBT_BRRES, "peachtreeGC.brres" },
	{ 2222, 221, 0x20068, 0, FF_BRRES, DBT_BRRES, "penguin_l.brres" },
	{ 2225, 222, 0x20068, 0, FF_BRRES, DBT_BRRES, "penguin_m.brres" },
	{ 2228, 223, 0x20068, 0, FF_BRRES, DBT_BRRES, "penguin_s.brres" },
	// 450
	{ 2231, 225, 0x20068, 0, FF_BRRES, DBT_BRRES, "pocha.brres" },
	{ 2262, 227, 0x20068, 0, FF_BRRES, DBT_BRRES, "pochaYogan.brres" },
	{ 2271, 228, 0x20068, 0, FF_BRRES, DBT_BRRES, "poihana.brres" },
	{ 2275, 258, 0x00804, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.bblm" },
	{ 2368, 258, 0x00804, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.bblm1" },
	{ 2375, 258, 0x00804, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.bdof" },
	{ 2462, 258, 0x00804, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.bfg" },
	{ 2511, 258, 0x00804, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.blight" },
	{ 2598, 258, 0x00804, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.blmap" },
	{ 2661, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.blobj" },
	// 460
	{ 2664, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.bltex" },
	{ 2667, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect.bti" },
	{ 2676, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect2.bblm" },
	{ 2679, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect2.blight" },
	{ 2682, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect2.blmap" },
	{ 2685, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect2.blobj" },
	{ 2688, 258, 0x20824, 0, FF_UNKNOWN, DBT_POSTEF, "posteffect/posteffect3.bblm" },
	{ 2691, 229, 0x20068, 0, FF_BRRES, DBT_BRRES, "puchi_pakkun.brres" },
	{ 2694, 230, 0x20068, 0, FF_BRRES, DBT_BRRES, "pukupuku.brres" },
	{ 2699, 231, 0x20068, 0, FF_BRRES, DBT_BRRES, "pylon01.brres" },
	// 470
	{ 2703, 232, 0x20068, 0, FF_BRRES, DBT_BRRES, "quicksand.brres" },
	{ 2706, 233, 0x20068, 0, FF_BRRES, DBT_BRRES, "r_parasol.brres" },
	{ 2709, 234, 0x20068, 0, FF_BRRES, DBT_BRRES, "ridgemii00.brres" },
	{ 2712, 260, 0x20220, 0, FF_KCL, DBT_KCL, "ring_mission_b.kcl" },
	{ 2715, 235, 0x20068, 0, FF_BRRES, DBT_BRRES, "sanbo.brres" },
	{ 2721, 236, 0x20068, 0, FF_BRRES, DBT_BRRES, "seagull.brres" },
	{ 2732, 237, 0x20068, 0, FF_BRRES, DBT_BRRES, "skyship.brres" },
	{ 2737, 238, 0x20068, 0, FF_BRRES, DBT_BRRES, "spot.brres" },
	{ 2740, 239, 0x50068, 0, FF_BRRES, DBT_BRRES, "starGate.brres" },
	{ 2742, 239, 0x50228, 0, FF_KCL, DBT_KCL, "starGate.kcl" },
	// 480
	{ 2744, 240, 0x20068, 0, FF_BRRES, DBT_BRRES, "sun.brres" },
	{ 2789, 241, 0x20068, 0, FF_BRRES, DBT_BRRES, "sunDS.brres" },
	{ 2792, 243, 0x20068, 0, FF_BRRES, DBT_BRRES, "tree_cannon.brres" },
	{ 2795, 244, 0x20068, 0, FF_BRRES, DBT_BRRES, "truckChimSmk.brres" },
	{ 2800, 245, 0x20068, 0, FF_BRRES, DBT_BRRES, "truckChimSmkW.brres" },
	{ 2803, 249, 0x20068, 0, FF_BRRES, DBT_BRRES, "venice_hasi.brres" },
	{ 2806, 249, 0x20228, 0, FF_KCL, DBT_KCL, "venice_hasi.kcl" },
	{ 2809, 260, 0x20220, 0, FF_KCL, DBT_KCL, "venice_hasi_b.kcl" },
	{ 2812, 260, 0x20220, 0, FF_KCL, DBT_KCL, "venice_hasi_wall.kcl" },
	{ 2815, 250, 0x20068, 0, FF_BRRES, DBT_BRRES, "venice_nami.brres" },
	// 490
	{ 2818, 251, 0x20068, 0, FF_BRRES, DBT_BRRES, "venice_saku.brres" },
	{ 2821, 251, 0x20228, 0, FF_KCL, DBT_KCL, "venice_saku.kcl" },
	{ 2824, 257, 0x00041, 0, FF_BRRES, DBT_BRRES, "vrcorn_model.brres" },
	{ 2924, 254, 0x20068, 0, FF_BRRES, DBT_BRRES, "wanwan.brres" },
	{ 2932, 255, 0x20068, 0, FF_BRRES, DBT_BRRES, "windmill.brres" },
	{ 2935, 256, 0x26068, 169, FF_BRRES, DBT_BRRES, "woodbox.brres" },
	// 496 = END
	{ -1, -1, 0, 0, 0 }
};

//
//--------------------------------------------------

const s16 DbFileRefFILE[N_DB_FILE_REF_FILE + 1] = {

	//    0, AIParam/AIParam.baa, N=1+1
	2441, -1,

	//    2, AIParam/AIParam.bas, N=1+1
	2442, -1,

	//    4, BGteresaSFC.brres, N=2+1
	1146, 1160, -1,

	//    7, BeltCrossing.brres, N=2+1
	152, 193, -1,

	//   10, BeltCurveA.brres, N=2+1
	153, 194, -1,

	//   13, BeltEasy.brres, N=2+1
	154, 195, -1,

	//   16, Block.brres, N=2+1
	155, 196, -1,

	//   19, CarA1.brres, N=2+1
	286, 312, -1,

	//   22, CarA2.brres, N=2+1
	287, 313, -1,

	//   25, CarA3.brres, N=2+1
	288, 314, -1,

	//   28, CarB.brres, N=2+1
	289, 315, -1,

	//   31, Crane.brres, N=2+1
	163, 204, -1,

	//   34, Crane.kcl, N=2+1
	164, 205, -1,

	//   37, Crescent64.brres, N=2+1
	2066, 2081, -1,

	//   40, DKrockGC.brres, N=3+1
	1668, 1693, 2436, -1,

	//   44, DKship64.brres, N=2+1
	1503, 1528, -1,

	//   47, DKtreeA64.brres, N=2+1
	1504, 1529, -1,

	//   50, DKtreeB64.brres, N=2+1
	1505, 1530, -1,

	//   53, DKturibashiGC.brres, N=2+1
	1669, 1694, -1,

	//   56, DemoCol.brres, N=2+1
	2206, 2219, -1,

	//   59, DemoCol.kcl, N=2+1
	2207, 2220, -1,

	//   62, DemoJugemu.brres, N=2+1
	2151, 2179, -1,

	//   65, DonkyCannonGC.brres, N=2+1
	1670, 1695, -1,

	//   68, DonkyCannon_wii.brres, N=2+1
	346, 376, -1,

	//   71, EarthRing.brres, N=2+1
	1012, 1033, -1,

	//   74, Epropeller.brres, N=2+1
	512, 549, -1,

	//   77, FireSnake.brres, N=4+1
	655, 732, 1411, 1436, -1,

	//   82, FlagA1.brres, N=2+1
	402, 424, -1,

	//   85, FlagA2.brres, N=2+1
	59, 85, -1,

	//   88, FlagA3.brres, N=2+1
	1319, 1347, -1,

	//   91, FlagA5.brres, N=2+1
	513, 550, -1,

	//   94, FlagB1.brres, N=2+1
	403, 425, -1,

	//   97, FlagB2.brres, N=4+1
	7, 33, 2232, 2253, -1,

	//  102, FlagB3.brres, N=2+1
	514, 551, -1,

	//  105, FlagB4.brres, N=2+1
	515, 552, -1,

	//  108, FlamePole.brres, N=2+1
	937, 975, -1,

	//  111, FlamePole.kcl, N=2+1
	938, 976, -1,

	//  114, FlamePole_v.brres, N=6+1
	656, 733, 939, 977, 1718, 1743, -1,

	//  121, HeyhoBallGBA.brres, N=2+1
	1270, 1295, -1,

	//  124, HeyhoShipGBA.brres, N=2+1
	1271, 1296, -1,

	//  127, HeyhoTreeGBA.brres, N=2+1
	1272, 1297, -1,

	//  130, InsekiA.brres, N=2+1
	1013, 1034, -1,

	//  133, InsekiB.brres, N=2+1
	1014, 1035, -1,

	//  136, K_bomb_car.brres, N=3+1
	887, 912, 2430, -1,

	//  140, K_bomb_car1.brres, N=1+1
	2431, -1,

	//  142, K_car_body.brres, N=2+1
	888, 913, -1,

	//  145, K_chairlift00.brres, N=2+1
	350, 380, -1,

	//  148, K_sticklift00.brres, N=2+1
	351, 381, -1,

	//  151, K_truck.brres, N=2+1
	889, 914, -1,

	//  154, KmoonZ.brres, N=2+1
	1016, 1037, -1,

	//  157, KoopaFigure64.brres, N=2+1
	1720, 1745, -1,

	//  160, Ksticketc.brres, N=2+1
	886, 911, -1,

	//  163, M_obj_jump.brres, N=2+1
	2381, 2390, -1,

	//  166, M_obj_jump.kcl, N=2+1
	2388, 2396, -1,

	//  169, M_obj_jump2.brres, N=1+1
	2387, -1,

	//  171, M_obj_jump2.kcl, N=1+1
	2383, -1,

	//  173, M_obj_s_jump.brres, N=2+1
	2384, 2392, -1,

	//  176, M_obj_s_jump.kcl, N=2+1
	2386, 2394, -1,

	//  179, M_obj_s_jump2.brres, N=1+1
	2382, -1,

	//  181, M_obj_s_jump2.kcl, N=1+1
	2385, -1,

	//  183, MarioGo64.brres, N=2+1
	1179, 1195, -1,

	//  186, MashBalloonGC.brres, N=4+1
	1557, 1580, 2274, 2287, -1,

	//  191, Mdush.brres, N=2+1
	246, 272, -1,

	//  194, Mdush.kcl, N=2+1
	247, 273, -1,

	//  197, MiiKanban.brres, N=4+1
	10, 36, 2235, 2256, -1,

	//  202, MiiObj01.brres, N=4+1
	11, 37, 2236, 2257, -1,

	//  207, MiiObj02.brres, N=4+1
	12, 38, 2237, 2258, -1,

	//  212, MiiObj03.brres, N=4+1
	13, 39, 2238, 2259, -1,

	//  217, MiiObjD01.brres, N=2+1
	456, 489, -1,

	//  220, MiiObjD02.brres, N=2+1
	457, 490, -1,

	//  223, MiiObjD03.brres, N=2+1
	458, 491, -1,

	//  226, MiiSighKino.brres, N=2+1
	168, 209, -1,

	//  229, MiiSignNoko.brres, N=2+1
	519, 556, -1,

	//  232, MiiSignWario.brres, N=2+1
	406, 428, -1,

	//  235, MiiSphinxY2.brres, N=2+1
	820, 858, -1,

	//  238, MiiStatueBD1.brres, N=2+1
	459, 492, -1,

	//  241, MiiStatueBL1.brres, N=2+1
	460, 493, -1,

	//  244, MiiStatueD1.brres, N=2+1
	461, 494, -1,

	//  247, MiiStatueD2.brres, N=2+1
	1792, 1832, -1,

	//  250, MiiStatueL1.brres, N=2+1
	462, 495, -1,

	//  253, MiiStatueL2.brres, N=2+1
	1793, 1833, -1,

	//  256, MiiStatueL3.brres, N=4+1
	14, 40, 2239, 2260, -1,

	//  261, MiiStatueM1.brres, N=2+1
	1794, 1834, -1,

	//  264, MiiStatueM2.brres, N=2+1
	353, 383, -1,

	//  267, MiiStatueP1.brres, N=2+1
	1795, 1835, -1,

	//  270, PalmTree.brres, N=2+1
	301, 327, -1,

	//  273, PeachHunsuiGC.brres, N=2+1
	1060, 1092, -1,

	//  276, Piston.brres, N=2+1
	169, 210, -1,

	//  279, Press.brres, N=2+1
	171, 212, -1,

	//  282, Psea.brres, N=2+1
	1064, 1096, -1,

	//  285, RM_ring1.brres, N=2+1
	2301, 2324, -1,

	//  288, RM_ring1.kcl, N=2+1
	2302, 2325, -1,

	//  291, RM_ring1b.kcl, N=2+1
	2303, 2326, -1,

	//  294, RM_ring2.kcl, N=2+1
	2304, 2327, -1,

	//  297, RM_ring2b.kcl, N=2+1
	2305, 2328, -1,

	//  300, RM_ring2c.kcl, N=2+1
	2306, 2329, -1,

	//  303, RM_ring3.kcl, N=2+1
	2307, 2330, -1,

	//  306, RM_ring3b.kcl, N=2+1
	2308, 2331, -1,

	//  309, RM_ring3c.kcl, N=2+1
	2309, 2332, -1,

	//  312, RhMiiKanban.brres, N=2+1
	892, 917, -1,

	//  315, SentakuDS.brres, N=2+1
	1326, 1354, -1,

	//  318, ShMiiObj01.brres, N=2+1
	303, 329, -1,

	//  321, ShMiiObj02.brres, N=2+1
	304, 330, -1,

	//  324, ShMiiObj03.brres, N=2+1
	305, 331, -1,

	//  327, SpaceSun.brres, N=2+1
	1018, 1039, -1,

	//  330, Spot64.brres, N=2+1
	2069, 2084, -1,

	//  333, StarRing.brres, N=2+1
	1019, 1040, -1,

	//  336, TownBridgeDS.brres, N=2+1
	1328, 1356, -1,

	//  339, TownBridgeDS.kcl, N=2+1
	1329, 1357, -1,

	//  342, TownBridgeDS2.kcl, N=2+1
	1330, 1358, -1,

	//  345, TownBridgeDS3.kcl, N=2+1
	1331, 1359, -1,

	//  348, TownTreeDS.brres, N=2+1
	1332, 1360, -1,

	//  351, TruckWagon.brres, N=2+1
	409, 431, -1,

	//  354, Twanwan.brres, N=3+1
	1951, 1970, 2409, -1,

	//  358, TwistedWay.brres, N=2+1
	946, 984, -1,

	//  361, UtsuboDokan.brres, N=2+1
	525, 562, -1,

	//  364, VolcanoBall1.brres, N=2+1
	660, 737, -1,

	//  367, VolcanoPiece0.kcl, N=2+1
	661, 738, -1,

	//  370, VolcanoPiece0b.kcl, N=2+1
	662, 739, -1,

	//  373, VolcanoPiece0c.kcl, N=2+1
	663, 740, -1,

	//  376, VolcanoPiece1.brres, N=2+1
	664, 741, -1,

	//  379, VolcanoPiece1.kcl, N=2+1
	665, 742, -1,

	//  382, VolcanoPiece11.kcl, N=2+1
	666, 743, -1,

	//  385, VolcanoPiece11b.kcl, N=2+1
	667, 744, -1,

	//  388, VolcanoPiece11c.kcl, N=2+1
	668, 745, -1,

	//  391, VolcanoPiece13.kcl, N=2+1
	669, 746, -1,

	//  394, VolcanoPiece13b.kcl, N=2+1
	670, 747, -1,

	//  397, VolcanoPiece13c.kcl, N=2+1
	671, 748, -1,

	//  400, VolcanoPiece14.kcl, N=2+1
	672, 749, -1,

	//  403, VolcanoPiece14b.kcl, N=2+1
	673, 750, -1,

	//  406, VolcanoPiece14c.kcl, N=2+1
	674, 751, -1,

	//  409, VolcanoPiece16.kcl, N=2+1
	675, 752, -1,

	//  412, VolcanoPiece16b.kcl, N=2+1
	676, 753, -1,

	//  415, VolcanoPiece16c.kcl, N=2+1
	677, 754, -1,

	//  418, VolcanoPiece18.kcl, N=2+1
	678, 755, -1,

	//  421, VolcanoPiece18b.kcl, N=2+1
	679, 756, -1,

	//  424, VolcanoPiece18c.kcl, N=2+1
	680, 757, -1,

	//  427, VolcanoPiece1b.kcl, N=2+1
	681, 758, -1,

	//  430, VolcanoPiece1c.kcl, N=2+1
	682, 759, -1,

	//  433, VolcanoPiece2.kcl, N=2+1
	683, 760, -1,

	//  436, VolcanoPiece2b.kcl, N=2+1
	684, 761, -1,

	//  439, VolcanoPiece2c.kcl, N=2+1
	685, 762, -1,

	//  442, VolcanoPiece3.kcl, N=2+1
	686, 763, -1,

	//  445, VolcanoPiece3b.kcl, N=2+1
	687, 764, -1,

	//  448, VolcanoPiece3c.kcl, N=2+1
	688, 765, -1,

	//  451, VolcanoPiece4.kcl, N=2+1
	689, 766, -1,

	//  454, VolcanoPiece4b.kcl, N=2+1
	690, 767, -1,

	//  457, VolcanoPiece4c.kcl, N=2+1
	691, 768, -1,

	//  460, VolcanoPiece5.kcl, N=2+1
	692, 769, -1,

	//  463, VolcanoPiece5b.kcl, N=2+1
	693, 770, -1,

	//  466, VolcanoPiece5c.kcl, N=2+1
	694, 771, -1,

	//  469, VolcanoPiece7.kcl, N=2+1
	695, 772, -1,

	//  472, VolcanoPiece7b.kcl, N=2+1
	696, 773, -1,

	//  475, VolcanoPiece7c.kcl, N=2+1
	697, 774, -1,

	//  478, VolcanoPiece8.kcl, N=2+1
	698, 775, -1,

	//  481, VolcanoPiece8b.kcl, N=2+1
	699, 776, -1,

	//  484, VolcanoPiece8c.kcl, N=2+1
	700, 777, -1,

	//  487, VolcanoPiece9.kcl, N=2+1
	701, 778, -1,

	//  490, VolcanoPiece9b.kcl, N=2+1
	702, 779, -1,

	//  493, VolcanoPiece9c.kcl, N=2+1
	703, 780, -1,

	//  496, VolcanoRock1.brres, N=2+1
	704, 781, -1,

	//  499, VolcanoRock1.kcl, N=2+1
	705, 782, -1,

	//  502, VolcanoRock2.kcl, N=2+1
	706, 783, -1,

	//  505, WLarrowGC.brres, N=2+1
	1378, 1396, -1,

	//  508, WLdokanGC.brres, N=2+1
	1379, 1397, -1,

	//  511, WLdokanGC.kcl, N=2+1
	1380, 1398, -1,

	//  514, WLfirebarGC.brres, N=4+1
	948, 986, 1381, 1399, -1,

	//  519, WLscreenGC.brres, N=2+1
	1382, 1400, -1,

	//  522, WLwallGC.brres, N=2+1
	1383, 1401, -1,

	//  525, WLwallGC.kcl, N=2+1
	1384, 1402, -1,

	//  528, ami.brres, N=2+1
	582, 617, -1,

	//  531, aurora.brres, N=2+1
	1008, 1029, -1,

	//  534, b_teresa.brres, N=2+1
	1147, 1161, -1,

	//  537, basabasa.brres, N=5+1
	398, 420, 806, 844, 2437, -1,

	//  543, bblock1.brres, N=2+1
	1764, 1804, -1,

	//  546, bblock1.kcl, N=2+1
	1765, 1805, -1,

	//  549, bblock1b.kcl, N=2+1
	1766, 1806, -1,

	//  552, bblock2.kcl, N=2+1
	1767, 1807, -1,

	//  555, bblock2b.kcl, N=2+1
	1768, 1808, -1,

	//  558, bblock3.kcl, N=2+1
	1769, 1809, -1,

	//  561, bblock3b.kcl, N=2+1
	1770, 1810, -1,

	//  564, bblock4.kcl, N=2+1
	1771, 1811, -1,

	//  567, bblock4b.kcl, N=2+1
	1772, 1812, -1,

	//  570, bblock5.kcl, N=2+1
	1773, 1813, -1,

	//  573, bblock5b.kcl, N=2+1
	1774, 1814, -1,

	//  576, bblock6.kcl, N=2+1
	1775, 1815, -1,

	//  579, bblock6b.kcl, N=2+1
	1776, 1816, -1,

	//  582, bblock7.kcl, N=2+1
	1777, 1817, -1,

	//  585, bblock7b.kcl, N=2+1
	1778, 1818, -1,

	//  588, bblock8.kcl, N=2+1
	1779, 1819, -1,

	//  591, bblock8b.kcl, N=2+1
	1780, 1820, -1,

	//  594, bblock9.kcl, N=2+1
	1781, 1821, -1,

	//  597, bblock9b.kcl, N=2+1
	1782, 1822, -1,

	//  600, begoman_spike.brres, N=2+1
	2294, 2317, -1,

	//  603, bird.brres, N=2+1
	1664, 1689, -1,

	//  606, boble.brres, N=2+1
	1458, 1479, -1,

	//  609, brasd/DonkyCannonGC/DonkyCannonGC.brasd, N=2+1
	1678, 1703, -1,

	//  612, brasd/DonkyCannon_wii/DonkyCannon_wii.brasd, N=2+1
	356, 386, -1,

	//  615, brasd/cow/eat.brasd, N=2+1
	65, 91, -1,

	//  618, brasd/cow/eat_ed.brasd, N=2+1
	66, 92, -1,

	//  621, brasd/cow/run.brasd, N=2+1
	67, 93, -1,

	//  624, brasd/cow/surprise.brasd, N=2+1
	68, 94, -1,

	//  627, brasd/cow/walk.brasd, N=2+1
	69, 95, -1,

	//  630, brasd/crab/wait.brasd, N=2+1
	1279, 1304, -1,

	//  633, brasd/crab/walk_l.brasd, N=2+1
	1280, 1305, -1,

	//  636, brasd/crab/walk_r.brasd, N=2+1
	1281, 1306, -1,

	//  639, brasd/hanachan/head_wait2.brasd, N=3+1
	594, 629, 2414, -1,

	//  643, brasd/hanachan/walk.brasd, N=3+1
	595, 630, 2415, -1,

	//  647, brasd/koopaFigure/vomit.brasd, N=2+1
	949, 987, -1,

	//  650, brasd/koopaFigure/wait.brasd, N=2+1
	950, 988, -1,

	//  653, brasd/kuribo/walk_l.brasd, N=11+1
	118, 142, 251, 277, 527, 564, 1563, 1586, 2277, 2290, 2405, -1,

	//  665, brasd/kuribo/walk_r.brasd, N=11+1
	119, 143, 252, 278, 528, 565, 1564, 1587, 2278, 2291, 2406, -1,

	//  677, brasd/pakkun_f/attack.brasd, N=4+1
	1565, 1588, 2279, 2292, -1,

	//  682, brasd/pakkun_f/wait.brasd, N=4+1
	1566, 1589, 2280, 2293, -1,

	//  687, brasd/penguin_m/walk.brasd, N=2+1
	1218, 1248, -1,

	//  690, brasd/penguin_s/dive.brasd, N=2+1
	1219, 1249, -1,

	//  693, brasd/penguin_s/stand_up.brasd, N=2+1
	1220, 1250, -1,

	//  696, brasd/penguin_s/walk.brasd, N=2+1
	1221, 1251, -1,

	//  699, brasd/poihana/throw.brasd, N=3+1
	1068, 1100, 2439, -1,

	//  703, brasd/poihana/walk.brasd, N=3+1
	1069, 1101, 2440, -1,

	//  707, brasd/puchi_pakkun/puchi_pakkun.brasd, N=2+1
	1183, 1199, -1,

	//  710, brasd/sanbo/revive.brasd, N=5+1
	825, 863, 1418, 1443, 2422, -1,

	//  716, brasd/tree_cannon/tree_cannon.brasd, N=2+1
	596, 631, -1,

	//  719, bulldozer_left.brres, N=2+1
	156, 197, -1,

	//  722, bulldozer_left.kcl, N=2+1
	157, 198, -1,

	//  725, bulldozer_right.brres, N=2+1
	158, 199, -1,

	//  728, bulldozer_right.kcl, N=2+1
	159, 200, -1,

	//  731, casino_roulette.brres, N=2+1
	1940, 1959, -1,

	//  734, casino_roulette.kcl, N=2+1
	1941, 1960, -1,

	//  737, castleballoon1.brres, N=4+1
	234, 260, 2144, 2172, -1,

	//  742, castleflower1.brres, N=8+1
	235, 261, 2145, 2173, 2200, 2213, 2340, 2351, -1,

	//  751, castletree1.brres, N=10+1
	52, 78, 236, 262, 2146, 2174, 2201, 2214, 2341, 2352, -1,

	//  762, castletree2.brres, N=10+1
	53, 79, 237, 263, 2147, 2175, 2202, 2215, 2342, 2353, -1,

	//  773, choropu.brres, N=4+1
	54, 80, 1620, 1642, -1,

	//  778, coin.brres, N=21+1
	1783, 1823, 1844, 1875, 1906, 1923, 1942, 1961, 1978, 1998, 2018, 2029, 2040, 2051, 2062, 2077,
	2092, 2103, 2114, 2129, 2380, -1,

	//  800, course.0, N=2+1
	932, 970, -1,

	//  803, course.bmm, N=12+1
	0, 26, 338, 368, 442, 475, 807, 845, 1784, 1824, 2226, 2247, -1,

	//  816, course.btiEnv, N=12+1
	1, 27, 339, 369, 443, 476, 808, 846, 1785, 1825, 2227, 2248, -1,

	//  829, course.btiMat, N=12+1
	2, 28, 340, 370, 444, 477, 809, 847, 1786, 1826, 2228, 2249, -1,

	//  842, course.kcl, N=99+1
	3, 29, 55, 81, 104, 128, 160, 201, 238, 264, 290, 316, 341, 371, 399, 421, 445, 478, 508, 545,
	583, 618, 652, 729, 810, 848, 882, 907, 933, 971, 1009, 1030, 1050, 1082, 1114, 1130, 1148,
	1162, 1174, 1190, 1206, 1236, 1266, 1291, 1316, 1344, 1372, 1390, 1408, 1433, 1459, 1480, 1500,
	1525, 1550, 1573, 1596, 1608, 1621, 1643, 1665, 1690, 1714, 1739, 1787, 1827, 1845, 1876, 1907,
	1924, 1943, 1962, 1979, 1999, 2019, 2030, 2041, 2052, 2063, 2078, 2093, 2104, 2115, 2130, 2148,
	2176, 2203, 2216, 2229, 2250, 2268, 2281, 2295, 2318, 2343, 2354, 2362, 2366, 2370, -1,

	//  942, course.kmp, N=99+1
	4, 30, 56, 82, 105, 129, 161, 202, 239, 265, 291, 317, 342, 372, 400, 422, 446, 479, 509, 546,
	584, 619, 653, 730, 811, 849, 883, 908, 934, 972, 1010, 1031, 1051, 1083, 1115, 1131, 1149,
	1163, 1175, 1191, 1207, 1237, 1267, 1292, 1317, 1345, 1373, 1391, 1409, 1434, 1460, 1481, 1501,
	1526, 1551, 1574, 1597, 1609, 1622, 1644, 1666, 1691, 1715, 1740, 1788, 1828, 1846, 1877, 1908,
	1925, 1944, 1963, 1980, 2000, 2020, 2031, 2042, 2053, 2064, 2079, 2094, 2105, 2116, 2131, 2149,
	2177, 2204, 2217, 2230, 2251, 2269, 2282, 2296, 2319, 2344, 2355, 2363, 2367, 2371, -1,

	// 1042, course.lex, N=1+1
	2444, -1,

	// 1044, course_d_model.brres, N=20+1
	31, 83, 130, 203, 266, 318, 423, 480, 547, 731, 850, 909, 973, 1032, 1878, 1964, 2001, 2178,
	2218, 2356, -1,

	// 1065, course_model.brres, N=79+1
	5, 57, 106, 162, 240, 292, 343, 373, 401, 447, 510, 585, 620, 654, 812, 884, 935, 1011, 1052,
	1084, 1116, 1132, 1150, 1164, 1176, 1192, 1208, 1238, 1268, 1293, 1318, 1346, 1374, 1392, 1410,
	1435, 1461, 1482, 1502, 1527, 1552, 1575, 1598, 1610, 1623, 1645, 1667, 1692, 1716, 1741, 1789,
	1829, 1847, 1909, 1926, 1945, 1981, 2021, 2032, 2043, 2054, 2065, 2080, 2095, 2106, 2117, 2132,
	2150, 2205, 2231, 2252, 2270, 2283, 2297, 2320, 2345, 2364, 2368, 2372, -1,

	// 1145, cow.brres, N=2+1
	58, 84, -1,

	// 1148, crab.brres, N=2+1
	1269, 1294, -1,

	// 1151, cruiser.brres, N=4+1
	448, 481, 511, 548, -1,

	// 1156, dc_pillar.brres, N=2+1
	813, 851, -1,

	// 1159, dc_pillar.kcl, N=2+1
	814, 852, -1,

	// 1162, dc_pillar_base.kcl, N=2+1
	815, 853, -1,

	// 1165, dc_sandcone.brres, N=2+1
	816, 854, -1,

	// 1168, dc_sandcone.kcl, N=2+1
	817, 855, -1,

	// 1171, dk_miiobj00.brres, N=2+1
	345, 375, -1,

	// 1174, dkmonitor.brres, N=2+1
	344, 374, -1,

	// 1177, dokan_sfc.brres, N=5+1
	6, 32, 1599, 1611, 2403, -1,

	// 1183, donkytree1GC.brres, N=2+1
	1671, 1696, -1,

	// 1186, donkytree2GC.brres, N=2+1
	1672, 1697, -1,

	// 1189, dossun.brres, N=9+1
	936, 974, 1462, 1483, 1717, 1742, 1982, 2002, 2425, -1,

	// 1199, effect/Alarm/rk_alarm.breff, N=2+1
	176, 217, -1,

	// 1202, effect/Alarm/rk_alarm.breft, N=2+1
	177, 218, -1,

	// 1205, effect/Block/rk_block.breff, N=2+1
	178, 219, -1,

	// 1208, effect/Block/rk_block.breft, N=2+1
	179, 220, -1,

	// 1211, effect/DKfalls/rk_64DKfalls.breff, N=2+1
	1512, 1537, -1,

	// 1214, effect/DKfalls/rk_64DKfalls.breft, N=2+1
	1513, 1538, -1,

	// 1217, effect/DKrockGC/rk_DKrockGC.breff, N=3+1
	1679, 1704, 2432, -1,

	// 1221, effect/DKrockGC/rk_DKrockGC.breft, N=3+1
	1680, 1705, 2433, -1,

	// 1225, effect/DKship64/rk_DKship64.breff, N=2+1
	1514, 1539, -1,

	// 1228, effect/DKship64/rk_DKship64.breft, N=2+1
	1515, 1540, -1,

	// 1231, effect/DemoEf/rk_demo.breff, N=2+1
	2160, 2188, -1,

	// 1234, effect/DemoEf/rk_demo.breft, N=2+1
	2161, 2189, -1,

	// 1237, effect/DemoJugemu/rk_DemoJugemu.breff, N=2+1
	2162, 2190, -1,

	// 1240, effect/DemoJugemu/rk_DemoJugemu.breft, N=2+1
	2163, 2191, -1,

	// 1243, effect/DonkyCannonGC/rk_dkCannon.breff, N=2+1
	1681, 1706, -1,

	// 1246, effect/DonkyCannonGC/rk_dkCannon.breft, N=2+1
	1682, 1707, -1,

	// 1249, effect/DonkyCannon_wii/rk_dkCannon.breff, N=2+1
	357, 387, -1,

	// 1252, effect/DonkyCannon_wii/rk_dkCannon.breft, N=2+1
	358, 388, -1,

	// 1255, effect/EnvFire/rk_EnvFire.breff, N=8+1
	708, 785, 953, 991, 1471, 1492, 1726, 1751, -1,

	// 1264, effect/EnvFire/rk_EnvFire.breft, N=8+1
	709, 786, 954, 992, 1472, 1493, 1727, 1752, -1,

	// 1273, effect/EnvKareha/rk_EnvKareha.breff, N=2+1
	597, 632, -1,

	// 1276, effect/EnvKareha/rk_EnvKareha.breft, N=2+1
	598, 633, -1,

	// 1279, effect/EnvKarehaUp/rk_EnvKarehaUp.breff, N=2+1
	599, 634, -1,

	// 1282, effect/EnvKarehaUp/rk_EnvKarehaUp.breft, N=2+1
	600, 635, -1,

	// 1285, effect/EnvSnow/rk_EnvSnow.breff, N=2+1
	359, 389, -1,

	// 1288, effect/EnvSnow/rk_EnvSnow.breft, N=2+1
	360, 390, -1,

	// 1291, effect/Epropeller/rk_epropeller.breff, N=2+1
	531, 568, -1,

	// 1294, effect/Epropeller/rk_epropeller.breft, N=2+1
	532, 569, -1,

	// 1297, effect/FallBsA/rk_fallbasin.breff, N=2+1
	533, 570, -1,

	// 1300, effect/FallBsA/rk_fallbasin.breft, N=2+1
	534, 571, -1,

	// 1303, effect/FallBsB/rk_fallbasin.breff, N=2+1
	535, 572, -1,

	// 1306, effect/FallBsB/rk_fallbasin.breft, N=2+1
	536, 573, -1,

	// 1309, effect/Fall_MH/rk_ridgeFall.breff, N=2+1
	895, 920, -1,

	// 1312, effect/Fall_MH/rk_ridgeFall.breft, N=2+1
	896, 921, -1,

	// 1315, effect/Fall_Y/rk_yoshiFalls.breff, N=2+1
	1122, 1138, -1,

	// 1318, effect/Fall_Y/rk_yoshiFalls.breft, N=2+1
	1123, 1139, -1,

	// 1321, effect/FireSnake/rk_fireSnake.breff, N=4+1
	710, 787, 1419, 1444, -1,

	// 1326, effect/FireSnake/rk_fireSnake.breft, N=4+1
	711, 788, 1420, 1445, -1,

	// 1331, effect/FlamePole/rk_flamePole.breff, N=2+1
	955, 993, -1,

	// 1334, effect/FlamePole/rk_flamePole.breft, N=2+1
	956, 994, -1,

	// 1337, effect/FlamePole_v/rk_flamePole.breff, N=6+1
	712, 789, 957, 995, 1728, 1753, -1,

	// 1344, effect/FlamePole_v/rk_flamePole.breft, N=6+1
	713, 790, 958, 996, 1729, 1754, -1,

	// 1351, effect/FlamePole_v_big/rk_flamePoleL.breff, N=2+1
	1730, 1755, -1,

	// 1354, effect/FlamePole_v_big/rk_flamePoleL.breft, N=2+1
	1731, 1756, -1,

	// 1357, effect/Flash_B/rk_flashBCross.breff, N=2+1
	361, 391, -1,

	// 1360, effect/Flash_B/rk_flashBCross.breft, N=2+1
	362, 392, -1,

	// 1363, effect/Flash_L/rk_flashBeginner.breff, N=2+1
	18, 44, -1,

	// 1366, effect/Flash_L/rk_flashBeginner.breft, N=2+1
	19, 45, -1,

	// 1369, effect/Flash_M/rk_flash64mario.breff, N=2+1
	1184, 1200, -1,

	// 1372, effect/Flash_M/rk_flash64mario.breft, N=2+1
	1185, 1201, -1,

	// 1375, effect/Flash_S/rk_flashSkate.breff, N=2+1
	1916, 1933, -1,

	// 1378, effect/Flash_S/rk_flashSkate.breft, N=2+1
	1917, 1934, -1,

	// 1381, effect/Flash_W/rk_flashWaluigi.breff, N=2+1
	1385, 1403, -1,

	// 1384, effect/Flash_W/rk_flashWaluigi.breft, N=2+1
	1386, 1404, -1,

	// 1387, effect/Hanabi/rk_stHanabi.breff, N=16+1
	20, 46, 253, 279, 469, 502, 830, 868, 1070, 1102, 1222, 1252, 1421, 1446, 1604, 1616, -1,

	// 1404, effect/Hanabi/rk_stHanabi.breft, N=16+1
	21, 47, 254, 280, 470, 503, 831, 869, 1071, 1103, 1223, 1253, 1422, 1447, 1605, 1617, -1,

	// 1421, effect/HeyhoBallGBA/rk_HeyhoBallGBA.breff, N=2+1
	1284, 1309, -1,

	// 1424, effect/HeyhoBallGBA/rk_HeyhoBallGBA.breft, N=2+1
	1285, 1310, -1,

	// 1427, effect/K_bomb_car/rk_K_bomb_car.breff, N=3+1
	897, 922, 2429, -1,

	// 1431, effect/K_bomb_car/rk_K_bomb_car.breft, N=3+1
	898, 923, 2428, -1,

	// 1435, effect/K_bomb_car1/rk_K_bomb_car.breff, N=1+1
	2427, -1,

	// 1437, effect/K_bomb_car1/rk_K_bomb_car.breft, N=1+1
	2426, -1,

	// 1439, effect/Kamifubuki/rk_kamifubuki.breff, N=2+1
	2164, 2192, -1,

	// 1442, effect/Kamifubuki/rk_kamifubuki.breft, N=2+1
	2165, 2193, -1,

	// 1445, effect/KoopaFigure64/rk_koopaFire.breff, N=2+1
	1732, 1757, -1,

	// 1448, effect/KoopaFigure64/rk_koopaFire.breft, N=2+1
	1733, 1758, -1,

	// 1451, effect/PeachHunsuiGC/rk_hunsui.breff, N=2+1
	1072, 1104, -1,

	// 1454, effect/PeachHunsuiGC/rk_hunsui.breft, N=2+1
	1073, 1105, -1,

	// 1457, effect/Press/rk_press.breff, N=2+1
	182, 223, -1,

	// 1460, effect/Press/rk_press.breft, N=2+1
	183, 224, -1,

	// 1463, effect/StarRing/rk_StarRing.breff, N=2+1
	1023, 1044, -1,

	// 1466, effect/StarRing/rk_StarRing.breft, N=2+1
	1024, 1045, -1,

	// 1469, effect/Steam/rk_steam.breff, N=2+1
	184, 225, -1,

	// 1472, effect/Steam/rk_steam.breft, N=2+1
	185, 226, -1,

	// 1475, effect/TruckWagon/rk_truck.breff, N=2+1
	413, 435, -1,

	// 1478, effect/TruckWagon/rk_truck.breft, N=2+1
	414, 436, -1,

	// 1481, effect/VolcanoBall1/rk_volc.breff, N=2+1
	718, 795, -1,

	// 1484, effect/VolcanoBall1/rk_volc.breft, N=2+1
	719, 796, -1,

	// 1487, effect/begoman_spike/rk_begoman_spike.breff, N=2+1
	2311, 2334, -1,

	// 1490, effect/begoman_spike/rk_begoman_spike.breft, N=2+1
	2312, 2335, -1,

	// 1493, effect/boble/rk_boble.breff, N=2+1
	1467, 1488, -1,

	// 1496, effect/boble/rk_boble.breft, N=2+1
	1468, 1489, -1,

	// 1499, effect/choropu/rk_choropuMoko.breff, N=4+1
	70, 96, 1631, 1653, -1,

	// 1504, effect/choropu/rk_choropuMoko.breft, N=4+1
	71, 97, 1632, 1654, -1,

	// 1509, effect/coin/rk_coin.breff, N=21+1
	1797, 1837, 1862, 1893, 1914, 1931, 1953, 1972, 1988, 2008, 2025, 2036, 2047, 2058, 2071, 2086,
	2099, 2110, 2121, 2136, 2378, -1,

	// 1531, effect/coin/rk_coin.breft, N=21+1
	1798, 1838, 1863, 1894, 1915, 1932, 1954, 1973, 1989, 2009, 2026, 2037, 2048, 2059, 2072, 2087,
	2100, 2111, 2122, 2137, 2379, -1,

	// 1553, effect/crab/rk_crab.breff, N=2+1
	1282, 1307, -1,

	// 1556, effect/crab/rk_crab.breft, N=2+1
	1283, 1308, -1,

	// 1559, effect/cruiser/rk_cruiser.breff, N=4+1
	467, 500, 529, 566, -1,

	// 1564, effect/cruiser/rk_cruiser.breft, N=4+1
	468, 501, 530, 567, -1,

	// 1569, effect/dc_pillar/rk_dc_pillar.breff, N=2+1
	826, 864, -1,

	// 1572, effect/dc_pillar/rk_dc_pillar.breft, N=2+1
	827, 865, -1,

	// 1575, effect/dc_sandcone/rk_dc_sandcone.breff, N=2+1
	828, 866, -1,

	// 1578, effect/dc_sandcone/rk_dc_sandcone.breft, N=2+1
	829, 867, -1,

	// 1581, effect/dossun/rk_dossun.breff, N=9+1
	951, 989, 1469, 1490, 1724, 1749, 1990, 2010, 2423, -1,

	// 1591, effect/dossun/rk_dossun.breft, N=9+1
	952, 990, 1470, 1491, 1725, 1750, 1991, 2011, 2424, -1,

	// 1601, effect/entry/rk_entry.breff, N=4+1
	1021, 1042, 2313, 2336, -1,

	// 1606, effect/entry/rk_entry.breft, N=4+1
	1022, 1043, 2314, 2337, -1,

	// 1611, effect/hanachan/rk_hana.breff, N=3+1
	601, 636, 2411, -1,

	// 1615, effect/hanachan/rk_hana.breft, N=3+1
	602, 637, 2412, -1,

	// 1619, effect/honeBall/rk_honeBall.breff, N=1+1
	2417, -1,

	// 1621, effect/honeBall/rk_honeBall.breft, N=1+1
	2416, -1,

	// 1623, effect/karehayama/rk_karehayama.breff, N=2+1
	603, 638, -1,

	// 1626, effect/karehayama/rk_karehayama.breft, N=2+1
	604, 639, -1,

	// 1629, effect/koopaBall/rk_koopaBall.breff, N=2+1
	959, 997, -1,

	// 1632, effect/koopaBall/rk_koopaBall.breft, N=2+1
	960, 998, -1,

	// 1635, effect/leaf_effect/rk_leaf_effect.breff, N=2+1
	605, 640, -1,

	// 1638, effect/leaf_effect/rk_leaf_effect.breft, N=2+1
	606, 641, -1,

	// 1641, effect/penguin_l/rk_penguin_l.breff, N=2+1
	1224, 1254, -1,

	// 1644, effect/penguin_l/rk_penguin_l.breft, N=2+1
	1225, 1255, -1,

	// 1647, effect/penguin_m/rk_penguin_m.breff, N=2+1
	1226, 1256, -1,

	// 1650, effect/penguin_m/rk_penguin_m.breft, N=2+1
	1227, 1257, -1,

	// 1653, effect/penguin_s/rk_penguin_s.breff, N=2+1
	1228, 1258, -1,

	// 1656, effect/penguin_s/rk_penguin_s.breft, N=2+1
	1229, 1259, -1,

	// 1659, effect/pocha/rk_pocha.breff, N=30+1
	180, 221, 537, 574, 607, 642, 832, 870, 899, 924, 1074, 1106, 1124, 1140, 1230, 1260, 1286,
	1311, 1335, 1363, 1423, 1448, 1516, 1541, 1633, 1655, 1683, 1708, 1864, 1895, -1,

	// 1690, effect/pocha/rk_pocha.breft, N=30+1
	181, 222, 538, 575, 608, 643, 833, 871, 900, 925, 1075, 1107, 1125, 1141, 1231, 1261, 1287,
	1312, 1336, 1364, 1424, 1449, 1517, 1542, 1634, 1656, 1684, 1709, 1865, 1896, -1,

	// 1721, effect/pochaMori/rk_pochaLeaf.breff, N=4+1
	901, 926, 2123, 2138, -1,

	// 1726, effect/pochaMori/rk_pochaLeaf.breft, N=4+1
	902, 927, 2124, 2139, -1,

	// 1731, effect/pochaYogan/rk_pochaYogan.breff, N=8+1
	714, 791, 961, 999, 1473, 1494, 1734, 1759, -1,

	// 1740, effect/pochaYogan/rk_pochaYogan.breft, N=8+1
	715, 792, 962, 1000, 1474, 1495, 1735, 1760, -1,

	// 1749, effect/poihana/rk_poihana.breff, N=3+1
	1076, 1108, 2434, -1,

	// 1753, effect/poihana/rk_poihana.breft, N=3+1
	1077, 1109, 2435, -1,

	// 1757, effect/quicksand/rk_qsandDossun.breff, N=2+1
	1992, 2012, -1,

	// 1760, effect/quicksand/rk_qsandDossun.breft, N=2+1
	1993, 2013, -1,

	// 1763, effect/sanbo/rk_sanbo.breff, N=5+1
	834, 872, 1425, 1450, 2419, -1,

	// 1769, effect/sanbo/rk_sanbo.breft, N=5+1
	835, 873, 1426, 1451, 2420, -1,

	// 1775, effect/starGate/rk_starGate.breff, N=1+1
	2375, -1,

	// 1777, effect/starGate/rk_starGate.breft, N=1+1
	2374, -1,

	// 1779, effect/taimatsu/rk_taimatsu.breff, N=8+1
	716, 793, 836, 874, 963, 1001, 1518, 1543, -1,

	// 1788, effect/taimatsu/rk_taimatsu.breft, N=8+1
	717, 794, 837, 875, 964, 1002, 1519, 1544, -1,

	// 1797, effect/tree_cannon/rk_dkCannon.breff, N=2+1
	609, 644, -1,

	// 1800, effect/tree_cannon/rk_dkCannon.breft, N=2+1
	610, 645, -1,

	// 1803, effect/truckChimSmk/rk_truckChimSmk.breff, N=4+1
	411, 433, 611, 646, -1,

	// 1808, effect/truckChimSmk/rk_truckChimSmk.breft, N=4+1
	412, 434, 612, 647, -1,

	// 1813, effect/truckChimSmkW/rk_truckChimSmkW.breff, N=2+1
	1866, 1897, -1,

	// 1816, effect/truckChimSmkW/rk_truckChimSmkW.breft, N=2+1
	1867, 1898, -1,

	// 1819, effect/venice_hasi/rk_venice.breff, N=2+1
	1868, 1899, -1,

	// 1822, effect/venice_hasi/rk_venice.breft, N=2+1
	1869, 1900, -1,

	// 1825, effect/volsmk/rk_volsmk.breff, N=2+1
	720, 797, -1,

	// 1828, effect/volsmk/rk_volsmk.breft, N=2+1
	721, 798, -1,

	// 1831, effect/wanwan/rk_wanwan.breff, N=7+1
	255, 281, 1567, 1590, 1635, 1657, 2408, -1,

	// 1839, effect/wanwan/rk_wanwan.breft, N=7+1
	256, 282, 1568, 1591, 1636, 1658, 2407, -1,

	// 1847, effect/woodbox/rk_woodbox.breff, N=5+1
	186, 227, 1337, 1365, 2398, -1,

	// 1853, effect/woodbox/rk_woodbox.breft, N=5+1
	187, 228, 1338, 1366, 2399, -1,

	// 1859, escalator.brres, N=2+1
	293, 319, -1,

	// 1862, escalator.kcl, N=2+1
	294, 320, -1,

	// 1865, fks_screen_wii.brres, N=2+1
	1910, 1927, -1,

	// 1868, gardentreeDS.brres, N=2+1
	1624, 1646, -1,

	// 1871, group_enemy_a.brres, N=8+1
	449, 482, 1053, 1085, 1506, 1531, 2152, 2180, -1,

	// 1880, group_enemy_a_demo.brres, N=2+1
	2153, 2181, -1,

	// 1883, group_enemy_b.brres, N=6+1
	450, 483, 1507, 1532, 2154, 2182, -1,

	// 1890, group_enemy_c.brres, N=6+1
	165, 206, 451, 484, 2155, 2183, -1,

	// 1897, group_enemy_e.brres, N=2+1
	241, 267, -1,

	// 1900, group_enemy_f.brres, N=2+1
	242, 268, -1,

	// 1903, group_monte_a.brres, N=12+1
	295, 321, 452, 485, 1054, 1086, 1320, 1348, 1848, 1879, 2156, 2184, -1,

	// 1916, group_monte_a_demo.brres, N=2+1
	2157, 2185, -1,

	// 1919, hanachan.brres, N=3+1
	586, 621, 2413, -1,

	// 1923, heyho.brres, N=2+1
	347, 377, -1,

	// 1926, heyho2.brres, N=2+1
	348, 378, -1,

	// 1929, honeBall.brres, N=1+1
	2418, -1,

	// 1931, ice.brres, N=2+1
	1209, 1239, -1,

	// 1934, info.txt, N=1+1
	2443, -1,

	// 1936, itembox.brres, N=88+1
	8, 34, 60, 86, 107, 131, 166, 207, 243, 269, 296, 322, 349, 379, 404, 426, 453, 486, 516, 553,
	587, 622, 657, 734, 818, 856, 885, 910, 940, 978, 1015, 1036, 1055, 1087, 1117, 1133, 1151,
	1165, 1177, 1193, 1210, 1240, 1273, 1298, 1321, 1349, 1375, 1393, 1412, 1437, 1463, 1484, 1508,
	1533, 1553, 1576, 1600, 1612, 1625, 1647, 1673, 1698, 1719, 1744, 1790, 1830, 1849, 1880, 1911,
	1928, 1946, 1965, 1983, 2003, 2022, 2033, 2044, 2055, 2067, 2082, 2096, 2107, 2118, 2133, 2233,
	2254, 2298, 2321, -1,

	// 2025, karehayama.brres, N=2+1
	588, 623, -1,

	// 2028, kinoko.brres, N=3+1
	108, 132, 2391, -1,

	// 2032, kinokoT1.brres, N=2+1
	109, 133, -1,

	// 2035, kinoko_d_g.kcl, N=3+1
	110, 134, 2393, -1,

	// 2039, kinoko_d_r.kcl, N=3+1
	111, 135, 2397, -1,

	// 2043, kinoko_g.kcl, N=3+1
	112, 136, 2389, -1,

	// 2047, kinoko_r.kcl, N=3+1
	113, 137, 2395, -1,

	// 2051, koopaBall.brres, N=2+1
	941, 979, -1,

	// 2054, koopaFigure.brres, N=2+1
	942, 980, -1,

	// 2057, koopaFirebar.brres, N=2+1
	943, 981, -1,

	// 2060, kuribo.brres, N=11+1
	114, 138, 244, 270, 517, 554, 1554, 1577, 2271, 2284, 2400, -1,

	// 2072, light_house.brres, N=2+1
	454, 487, -1,

	// 2075, map_model.brres, N=96+1
	9, 35, 61, 87, 115, 139, 167, 208, 245, 271, 297, 323, 352, 382, 405, 427, 455, 488, 518, 555,
	589, 624, 658, 735, 819, 857, 890, 915, 944, 982, 1017, 1038, 1056, 1088, 1118, 1134, 1152,
	1166, 1178, 1194, 1211, 1241, 1274, 1299, 1322, 1350, 1376, 1394, 1413, 1438, 1464, 1485, 1509,
	1534, 1555, 1578, 1601, 1613, 1626, 1648, 1674, 1699, 1721, 1746, 1791, 1831, 1850, 1881, 1912,
	1929, 1947, 1966, 1984, 2004, 2023, 2034, 2045, 2056, 2068, 2083, 2097, 2108, 2119, 2134, 2158,
	2186, 2208, 2221, 2234, 2255, 2272, 2285, 2299, 2322, 2346, 2357, -1,

	// 2172, mare_a.brres, N=2+1
	1057, 1089, -1,

	// 2175, mare_b.brres, N=2+1
	1058, 1090, -1,

	// 2178, mariotreeGC.brres, N=4+1
	1556, 1579, 2273, 2286, -1,

	// 2183, mii_balloon.brres, N=2+1
	299, 325, -1,

	// 2186, miiposter.brres, N=2+1
	298, 324, -1,

	// 2189, monte_a.brres, N=6+1
	300, 326, 1059, 1091, 1323, 1351, -1,

	// 2196, moray.brres, N=4+1
	520, 557, 1948, 1967, -1,

	// 2201, obakeblockSFC.brres, N=2+1
	1153, 1167, -1,

	// 2204, oilSFC.brres, N=3+1
	1602, 1614, 2404, -1,

	// 2208, pakkun_dokan.brres, N=2+1
	1558, 1581, -1,

	// 2211, pakkun_f.brres, N=4+1
	1559, 1582, 2275, 2288, -1,

	// 2216, parasol.brres, N=2+1
	302, 328, -1,

	// 2219, peachtreeGC.brres, N=2+1
	1061, 1093, -1,

	// 2222, penguin_l.brres, N=2+1
	1212, 1242, -1,

	// 2225, penguin_m.brres, N=2+1
	1213, 1243, -1,

	// 2228, penguin_s.brres, N=2+1
	1214, 1244, -1,

	// 2231, pocha.brres, N=30+1
	170, 211, 521, 558, 590, 625, 821, 859, 891, 916, 1062, 1094, 1119, 1135, 1215, 1245, 1275,
	1300, 1324, 1352, 1414, 1439, 1510, 1535, 1627, 1649, 1675, 1700, 1851, 1882, -1,

	// 2262, pochaYogan.brres, N=8+1
	659, 736, 945, 983, 1465, 1486, 1722, 1747, -1,

	// 2271, poihana.brres, N=3+1
	1063, 1095, 2438, -1,

	// 2275, posteffect/posteffect.bblm, N=92+1
	22, 48, 72, 98, 120, 144, 188, 229, 257, 283, 308, 334, 363, 393, 415, 437, 471, 504, 539, 576,
	613, 648, 722, 799, 838, 876, 903, 928, 965, 1003, 1025, 1046, 1078, 1110, 1126, 1142, 1155,
	1169, 1186, 1202, 1232, 1262, 1288, 1313, 1339, 1367, 1387, 1405, 1427, 1452, 1475, 1496, 1520,
	1545, 1569, 1592, 1606, 1618, 1637, 1659, 1685, 1710, 1736, 1761, 1799, 1839, 1870, 1901, 1918,
	1935, 1955, 1974, 1994, 2014, 2027, 2038, 2049, 2060, 2073, 2088, 2101, 2112, 2125, 2140, 2166,
	2194, 2210, 2223, 2242, 2263, 2348, 2359, -1,

	// 2368, posteffect/posteffect.bblm1, N=6+1
	121, 145, 540, 577, 723, 800, -1,

	// 2375, posteffect/posteffect.bdof, N=86+1
	23, 49, 73, 99, 122, 146, 189, 230, 258, 284, 309, 335, 364, 394, 416, 438, 472, 505, 541, 578,
	614, 649, 724, 801, 839, 877, 904, 929, 966, 1004, 1026, 1047, 1079, 1111, 1127, 1143, 1156,
	1170, 1187, 1203, 1233, 1263, 1289, 1314, 1340, 1368, 1388, 1406, 1428, 1453, 1476, 1497, 1521,
	1546, 1570, 1593, 1607, 1619, 1638, 1660, 1686, 1711, 1737, 1762, 1800, 1840, 1871, 1902, 1919,
	1936, 1956, 1975, 1995, 2015, 2028, 2039, 2050, 2061, 2074, 2089, 2102, 2113, 2126, 2141, 2243,
	2264, -1,

	// 2462, posteffect/posteffect.bfg, N=48+1
	74, 100, 123, 147, 190, 231, 365, 395, 417, 439, 542, 579, 725, 802, 840, 878, 967, 1005, 1080,
	1112, 1128, 1144, 1157, 1171, 1188, 1204, 1234, 1264, 1341, 1369, 1429, 1454, 1477, 1498, 1522,
	1547, 1571, 1594, 1639, 1661, 1687, 1712, 1801, 1841, 1872, 1903, 1920, 1937, -1,

	// 2511, posteffect/posteffect.blight, N=86+1
	24, 50, 75, 101, 124, 148, 191, 232, 259, 285, 310, 336, 366, 396, 418, 440, 473, 506, 543, 580,
	615, 650, 726, 803, 841, 879, 905, 930, 968, 1006, 1027, 1048, 1081, 1113, 1129, 1145, 1158,
	1172, 1189, 1205, 1235, 1265, 1290, 1315, 1342, 1370, 1389, 1407, 1430, 1455, 1478, 1499, 1523,
	1548, 1572, 1595, 1640, 1662, 1688, 1713, 1738, 1763, 1802, 1842, 1873, 1904, 1921, 1938, 1957,
	1976, 1996, 2016, 2075, 2090, 2127, 2142, 2167, 2195, 2211, 2224, 2244, 2265, 2315, 2338, 2349,
	2360, -1,

	// 2598, posteffect/posteffect.blmap, N=62+1
	25, 51, 125, 149, 192, 233, 311, 337, 367, 397, 419, 441, 474, 507, 544, 581, 616, 651, 727,
	804, 842, 880, 906, 931, 969, 1007, 1028, 1049, 1159, 1173, 1343, 1371, 1431, 1456, 1524, 1549,
	1641, 1663, 1803, 1843, 1874, 1905, 1922, 1939, 1958, 1977, 1997, 2017, 2076, 2091, 2128, 2143,
	2168, 2196, 2212, 2225, 2245, 2266, 2316, 2339, 2350, 2361, -1,

	// 2661, posteffect/posteffect.blobj, N=2+1
	2169, 2197, -1,

	// 2664, posteffect/posteffect.bltex, N=2+1
	2170, 2198, -1,

	// 2667, posteffect/posteffect.bti, N=8+1
	728, 805, 843, 881, 1432, 1457, 2246, 2267, -1,

	// 2676, posteffect/posteffect2.bblm, N=2+1
	76, 102, -1,

	// 2679, posteffect/posteffect2.blight, N=2+1
	126, 150, -1,

	// 2682, posteffect/posteffect2.blmap, N=2+1
	127, 151, -1,

	// 2685, posteffect/posteffect2.blobj, N=2+1
	2171, 2199, -1,

	// 2688, posteffect/posteffect3.bblm, N=2+1
	77, 103, -1,

	// 2691, puchi_pakkun.brres, N=2+1
	1180, 1196, -1,

	// 2694, pukupuku.brres, N=4+1
	522, 559, 1949, 1968, -1,

	// 2699, pylon01.brres, N=3+1
	463, 496, 2402, -1,

	// 2703, quicksand.brres, N=2+1
	1985, 2005, -1,

	// 2706, r_parasol.brres, N=2+1
	1276, 1301, -1,

	// 2709, ridgemii00.brres, N=2+1
	893, 918, -1,

	// 2712, ring_mission_b.kcl, N=2+1
	2300, 2323, -1,

	// 2715, sanbo.brres, N=5+1
	822, 860, 1415, 1440, 2421, -1,

	// 2721, seagull.brres, N=10+1
	172, 213, 464, 497, 523, 560, 1065, 1097, 1325, 1353, -1,

	// 2732, skyship.brres, N=4+1
	15, 41, 2240, 2261, -1,

	// 2737, spot.brres, N=2+1
	1950, 1969, -1,

	// 2740, starGate.brres, N=1+1
	2376, -1,

	// 2742, starGate.kcl, N=1+1
	2377, -1,

	// 2744, sun.brres, N=44+1
	16, 42, 62, 88, 116, 140, 173, 214, 248, 274, 306, 332, 354, 384, 407, 429, 465, 498, 524, 561,
	823, 861, 1066, 1098, 1120, 1136, 1181, 1197, 1216, 1246, 1277, 1302, 1327, 1355, 1560, 1583,
	1628, 1650, 1676, 1701, 1852, 1883, 1986, 2006, -1,

	// 2789, sunDS.brres, N=2+1
	1416, 1441, -1,

	// 2792, tree_cannon.brres, N=2+1
	591, 626, -1,

	// 2795, truckChimSmk.brres, N=4+1
	408, 430, 592, 627, -1,

	// 2800, truckChimSmkW.brres, N=2+1
	1853, 1884, -1,

	// 2803, venice_hasi.brres, N=2+1
	1854, 1885, -1,

	// 2806, venice_hasi.kcl, N=2+1
	1855, 1886, -1,

	// 2809, venice_hasi_b.kcl, N=2+1
	1856, 1887, -1,

	// 2812, venice_hasi_wall.kcl, N=2+1
	1857, 1888, -1,

	// 2815, venice_nami.brres, N=2+1
	1858, 1889, -1,

	// 2818, venice_saku.brres, N=2+1
	1859, 1890, -1,

	// 2821, venice_saku.kcl, N=2+1
	1860, 1891, -1,

	// 2824, vrcorn_model.brres, N=99+1
	17, 43, 63, 89, 117, 141, 174, 215, 249, 275, 307, 333, 355, 385, 410, 432, 466, 499, 526, 563,
	593, 628, 707, 784, 824, 862, 894, 919, 947, 985, 1020, 1041, 1067, 1099, 1121, 1137, 1154,
	1168, 1182, 1198, 1217, 1247, 1278, 1303, 1333, 1361, 1377, 1395, 1417, 1442, 1466, 1487, 1511,
	1536, 1561, 1584, 1603, 1615, 1629, 1651, 1677, 1702, 1723, 1748, 1796, 1836, 1861, 1892, 1913,
	1930, 1952, 1971, 1987, 2007, 2024, 2035, 2046, 2057, 2070, 2085, 2098, 2109, 2120, 2135, 2159,
	2187, 2209, 2222, 2241, 2262, 2276, 2289, 2310, 2333, 2347, 2358, 2365, 2369, 2373, -1,

	// 2924, wanwan.brres, N=7+1
	250, 276, 1562, 1585, 1630, 1652, 2410, -1,

	// 2932, windmill.brres, N=2+1
	64, 90, -1,

	// 2935, woodbox.brres, N=5+1
	175, 216, 1334, 1362, 2401, -1,

	// END
	-1
};

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////   group   //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const DbFileGROUP_t DbFileGROUP[N_DB_FILE_GROUP + 1] = { { 0x000a8, 0, "E", "Alarm" }, //   0
	{ 0x00068, 3, "B", "BGteresaSFC" }, //   1
	{ 0x00068, 5, "B", "BeltCrossing" }, //   2
	{ 0x00068, 7, "B", "BeltCurveA" }, //   3
	{ 0x01008, 9, "", "BeltCurveB" }, //   4
	{ 0x00068, 10, "B", "BeltEasy" }, //   5
	{ 0x01008, 12, "", "BlackHole" }, //   6
	{ 0x000e8, 13, "BE", "Block" }, //   7
	{ 0x01008, 17, "", "BossField01_OBJ1" }, //   8
	{ 0x00068, 18, "B", "CarA1" }, //   9
	{ 0x00068, 20, "B", "CarA2" }, //  10
	{ 0x00068, 22, "B", "CarA3" }, //  11
	{ 0x00068, 24, "B", "CarB" }, //  12
	{ 0x00268, 26, "BK", "Crane" }, //  13
	{ 0x00068, 29, "B", "Crescent64" }, //  14
	{ 0x000a8, 31, "E", "DKfalls" }, //  15
	{ 0x01008, 34, "", "DKmarutaGC" }, //  16
	{ 0x000e8, 35, "BE", "DKrockGC" }, //  17
	{ 0x01008, 39, "", "DKropeGC" }, //  18
	{ 0x000e8, 40, "BE", "DKship64" }, //  19
	{ 0x06068, 44, "B", "DKtreeA64" }, //  20
	{ 0x06068, 46, "B", "DKtreeB64" }, //  21
	{ 0x0e068, 48, "B", "DKturibashiGC" }, //  22
	{ 0x00268, 50, "BK", "DemoCol" }, //  23
	{ 0x000a8, 53, "E", "DemoEf" }, //  24
	{ 0x000e8, 56, "BE", "DemoJugemu" }, //  25
	{ 0x001e8, 60, "BED", "DonkyCannonGC" }, //  26
	{ 0x001e8, 65, "BED", "DonkyCannon_wii" }, //  27
	{ 0x00068, 70, "B", "EarthRing" }, //  28
	{ 0x000a8, 72, "E", "EnvFire" }, //  29
	{ 0x000a8, 75, "E", "EnvKareha" }, //  30
	{ 0x000a8, 78, "E", "EnvKarehaUp" }, //  31
	{ 0x000a8, 81, "E", "EnvSnow" }, //  32
	{ 0x000e8, 84, "BE", "Epropeller" }, //  33
	{ 0x000a8, 88, "E", "FallBsA" }, //  34
	{ 0x000a8, 91, "E", "FallBsB" }, //  35
	{ 0x01008, 94, "", "FallBsC" }, //  36
	{ 0x000a8, 95, "E", "Fall_MH" }, //  37
	{ 0x000a8, 98, "E", "Fall_Y" }, //  38
	{ 0x000e8, 101, "BE", "FireSnake" }, //  39
	{ 0x00068, 105, "B", "FlagA1" }, //  40
	{ 0x00068, 107, "B", "FlagA2" }, //  41
	{ 0x00068, 109, "B", "FlagA3" }, //  42
	{ 0x01008, 111, "", "FlagA4" }, //  43
	{ 0x00068, 112, "B", "FlagA5" }, //  44
	{ 0x00068, 114, "B", "FlagB1" }, //  45
	{ 0x00068, 116, "B", "FlagB2" }, //  46
	{ 0x00068, 118, "B", "FlagB3" }, //  47
	{ 0x00068, 120, "B", "FlagB4" }, //  48
	{ 0x002e8, 122, "BEK", "FlamePole" }, //  49
	{ 0x01008, 127, "", "FlamePoleEff" }, //  50
	{ 0x000e8, 128, "BE", "FlamePole_v" }, //  51
	{ 0x000a8, 132, "E", "FlamePole_v_big" }, //  52
	{ 0x000a8, 135, "E", "Flash_B" }, //  53
	{ 0x000a8, 138, "E", "Flash_L" }, //  54
	{ 0x000a8, 141, "E", "Flash_M" }, //  55
	{ 0x000a8, 144, "E", "Flash_S" }, //  56
	{ 0x000a8, 147, "E", "Flash_W" }, //  57
	{ 0x01008, 150, "", "HP_pipe" }, //  58
	{ 0x000a8, 151, "E", "Hanabi" }, //  59
	{ 0x000e8, 154, "BE", "HeyhoBallGBA" }, //  60
	{ 0x00068, 158, "B", "HeyhoShipGBA" }, //  61
	{ 0x06068, 160, "B", "HeyhoTreeGBA" }, //  62
	{ 0x00068, 162, "B", "InsekiA" }, //  63
	{ 0x00068, 164, "B", "InsekiB" }, //  64
	{ 0x000e8, 166, "BE", "K_bomb_car" }, //  65
	{ 0x100e8, 170, "BE", "K_bomb_car1" }, //  66
	{ 0x01008, 174, "", "K_bomb_car_dummy" }, //  67
	{ 0x00068, 175, "B", "K_car_body" }, //  68
	{ 0x00068, 177, "B", "K_chairlift00" }, //  69
	{ 0x00068, 179, "B", "K_sticklift00" }, //  70
	{ 0x00068, 181, "B", "K_truck" }, //  71
	{ 0x000a8, 183, "E", "Kamifubuki" }, //  72
	{ 0x00068, 186, "B", "KmoonZ" }, //  73
	{ 0x000e8, 188, "BE", "KoopaFigure64" }, //  74
	{ 0x00068, 192, "B", "Ksticketc" }, //  75
	{ 0x01008, 194, "", "MFaceBill" }, //  76
	{ 0x10268, 195, "BK", "M_obj_jump" }, //  77
	{ 0x10268, 198, "BK", "M_obj_jump2" }, //  78
	{ 0x01008, 201, "", "M_obj_kanban" }, //  79
	{ 0x01008, 202, "", "M_obj_kanban_y" }, //  80
	{ 0x10268, 203, "BK", "M_obj_s_jump" }, //  81
	{ 0x10268, 206, "BK", "M_obj_s_jump2" }, //  82
	{ 0x01008, 209, "", "M_obj_start" }, //  83
	{ 0x00068, 210, "B", "MarioGo64" }, //  84
	{ 0x00068, 212, "B", "MashBalloonGC" }, //  85
	{ 0x00268, 214, "BK", "Mdush" }, //  86
	{ 0x00068, 217, "B", "MiiKanban" }, //  87
	{ 0x00068, 219, "B", "MiiObj01" }, //  88
	{ 0x00068, 221, "B", "MiiObj02" }, //  89
	{ 0x00068, 223, "B", "MiiObj03" }, //  90
	{ 0x00068, 225, "B", "MiiObjD01" }, //  91
	{ 0x00068, 227, "B", "MiiObjD02" }, //  92
	{ 0x00068, 229, "B", "MiiObjD03" }, //  93
	{ 0x00068, 231, "B", "MiiSighKino" }, //  94
	{ 0x00068, 233, "B", "MiiSignNoko" }, //  95
	{ 0x00068, 235, "B", "MiiSignWario" }, //  96
	{ 0x01008, 237, "", "MiiSphinxY1" }, //  97
	{ 0x00068, 238, "B", "MiiSphinxY2" }, //  98
	{ 0x01008, 240, "", "MiiStatue1" }, //  99
	{ 0x00068, 241, "B", "MiiStatueBD1" }, // 100
	{ 0x00068, 243, "B", "MiiStatueBL1" }, // 101
	{ 0x00068, 245, "B", "MiiStatueD1" }, // 102
	{ 0x00068, 247, "B", "MiiStatueD2" }, // 103
	{ 0x01008, 249, "", "MiiStatueDK1" }, // 104
	{ 0x00068, 250, "B", "MiiStatueL1" }, // 105
	{ 0x00068, 252, "B", "MiiStatueL2" }, // 106
	{ 0x00068, 254, "B", "MiiStatueL3" }, // 107
	{ 0x00068, 256, "B", "MiiStatueM1" }, // 108
	{ 0x00068, 258, "B", "MiiStatueM2" }, // 109
	{ 0x00068, 260, "B", "MiiStatueP1" }, // 110
	{ 0x01008, 262, "", "Mstand" }, // 111
	{ 0x06068, 263, "B", "PalmTree" }, // 112
	{ 0x000e8, 265, "BE", "PeachHunsuiGC" }, // 113
	{ 0x00068, 269, "B", "Piston" }, // 114
	{ 0x000e8, 271, "BE", "Press" }, // 115
	{ 0x00068, 275, "B", "Psea" }, // 116
	{ 0x0026c, 277, "B8K", "RM_ring1" }, // 117
	{ 0x00068, 287, "B", "RhMiiKanban" }, // 118
	{ 0x00068, 289, "B", "SentakuDS" }, // 119
	{ 0x00068, 291, "B", "ShMiiObj01" }, // 120
	{ 0x00068, 293, "B", "ShMiiObj02" }, // 121
	{ 0x00068, 295, "B", "ShMiiObj03" }, // 122
	{ 0x00068, 297, "B", "SpaceSun" }, // 123
	{ 0x00068, 299, "B", "Spot64" }, // 124
	{ 0x000e8, 301, "BE", "StarRing" }, // 125
	{ 0x000a8, 305, "E", "Steam" }, // 126
	{ 0x00268, 308, "B3K", "TownBridgeDS" }, // 127
	{ 0x06068, 313, "B", "TownTreeDS" }, // 128
	{ 0x000e8, 315, "BE", "TruckWagon" }, // 129
	{ 0x00068, 319, "B", "Twanwan" }, // 130
	{ 0x01008, 321, "", "Twanwan_ue" }, // 131
	{ 0x00068, 322, "B", "TwistedWay" }, // 132
	{ 0x00068, 324, "B", "UtsuboDokan" }, // 133
	{ 0x000e8, 326, "BE", "VolcanoBall1" }, // 134
	{ 0x0026c, 330, "B42K", "VolcanoPiece1" }, // 135
	{ 0x00268, 374, "B2K", "VolcanoRock1" }, // 136
	{ 0x00068, 378, "B", "WLarrowGC" }, // 137
	{ 0x06268, 380, "BK", "WLdokanGC" }, // 138
	{ 0x00068, 383, "B", "WLfirebarGC" }, // 139
	{ 0x06068, 385, "B", "WLscreenGC" }, // 140
	{ 0x00268, 387, "BK", "WLwallGC" }, // 141
	{ 0x00068, 390, "B", "ami" }, // 142
	{ 0x00068, 392, "B", "aurora" }, // 143
	{ 0x00068, 394, "B", "b_teresa" }, // 144
	{ 0x00068, 396, "B", "basabasa" }, // 145
	{ 0x0026c, 398, "B18K", "bblock1" }, // 146
	{ 0x000e8, 418, "BE", "begoman_spike" }, // 147
	{ 0x00068, 422, "B", "bird" }, // 148
	{ 0x000e8, 424, "BE", "boble" }, // 149
	{ 0x00268, 428, "BK", "bulldozer_left" }, // 150
	{ 0x00268, 431, "BK", "bulldozer_right" }, // 151
	{ 0x01008, 434, "", "car_body_dummy" }, // 152
	{ 0x00268, 435, "BK", "casino_roulette" }, // 153
	{ 0x00068, 438, "B", "castleballoon1" }, // 154
	{ 0x00048, 440, "B", "castleflower1" }, // 155
	{ 0x06048, 442, "B", "castletree1" }, // 156
	{ 0x06048, 444, "B", "castletree2" }, // 157
	{ 0x000e8, 446, "BE", "choropu" }, // 158
	{ 0x060e8, 450, "BE", "coin" }, // 159
	{ 0x00168, 454, "B5D", "cow" }, // 160
	{ 0x001e8, 461, "BE3D", "crab" }, // 161
	{ 0x000e8, 468, "BE", "cruiser" }, // 162
	{ 0x002e8, 472, "BEK", "dc_pillar" }, // 163
	{ 0x002e8, 477, "BEK", "dc_sandcone" }, // 164
	{ 0x00068, 482, "B", "dk_miiobj00" }, // 165
	{ 0x00068, 484, "B", "dkmonitor" }, // 166
	{ 0x06068, 486, "B", "dokan_sfc" }, // 167
	{ 0x06068, 488, "B", "donkytree1GC" }, // 168
	{ 0x06068, 490, "B", "donkytree2GC" }, // 169
	{ 0x000e8, 492, "BE", "dossun" }, // 170
	{ 0x01008, 496, "", "dummy" }, // 171
	{ 0x000a8, 497, "E", "entry" }, // 172
	{ 0x00268, 500, "BK", "escalator" }, // 173
	{ 0x00068, 503, "B", "fks_screen_wii" }, // 174
	{ 0x01008, 505, "", "flag" }, // 175
	{ 0x01008, 506, "", "flagBlend" }, // 176
	{ 0x06068, 507, "B", "gardentreeDS" }, // 177
	{ 0x00068, 509, "B", "group_enemy_a" }, // 178
	{ 0x00068, 511, "B", "group_enemy_a_demo" }, // 179
	{ 0x00068, 513, "B", "group_enemy_b" }, // 180
	{ 0x00068, 515, "B", "group_enemy_c" }, // 181
	{ 0x01008, 517, "", "group_enemy_d" }, // 182
	{ 0x00068, 518, "B", "group_enemy_e" }, // 183
	{ 0x00068, 520, "B", "group_enemy_f" }, // 184
	{ 0x00068, 522, "B", "group_monte_a" }, // 185
	{ 0x00068, 524, "B", "group_monte_a_demo" }, // 186
	{ 0x001e8, 526, "BE2D", "hanachan" }, // 187
	{ 0x00068, 532, "B", "heyho" }, // 188
	{ 0x00068, 534, "B", "heyho2" }, // 189
	{ 0x100e8, 536, "BE", "honeBall" }, // 190
	{ 0x0006c, 540, "B", "ice" }, // 191
	{ 0x00068, 542, "B", "itembox" }, // 192
	{ 0x060e8, 544, "BE", "karehayama" }, // 193
	{ 0x01008, 548, "", "kart_truck_dummy" }, // 194
	{ 0x0e268, 549, "B4K", "kinoko" }, // 195
	{ 0x00068, 555, "B", "kinokoT1" }, // 196
	{ 0x01008, 557, "", "kinokoT2" }, // 197
	{ 0x000e8, 558, "BE", "koopaBall" }, // 198
	{ 0x00168, 562, "B2D", "koopaFigure" }, // 199
	{ 0x00068, 566, "B", "koopaFirebar" }, // 200
	{ 0x00168, 568, "B2D", "kuribo" }, // 201
	{ 0x000a8, 572, "E", "leaf_effect" }, // 202
	{ 0x01008, 575, "", "lensFX" }, // 203
	{ 0x00068, 576, "B", "light_house" }, // 204
	{ 0x00068, 578, "B", "mare_a" }, // 205
	{ 0x00068, 580, "B", "mare_b" }, // 206
	{ 0x06068, 582, "B", "mariotreeGC" }, // 207
	{ 0x00068, 584, "B", "mii_balloon" }, // 208
	{ 0x00068, 586, "B", "miiposter" }, // 209
	{ 0x00068, 588, "B", "monte_a" }, // 210
	{ 0x00068, 590, "B", "moray" }, // 211
	{ 0x01008, 592, "", "npc_mii_a" }, // 212
	{ 0x01008, 593, "", "npc_mii_b" }, // 213
	{ 0x01008, 594, "", "npc_mii_c" }, // 214
	{ 0x06068, 595, "B", "obakeblockSFC" }, // 215
	{ 0x00068, 597, "B", "oilSFC" }, // 216
	{ 0x06068, 599, "B", "pakkun_dokan" }, // 217
	{ 0x00168, 601, "B2D", "pakkun_f" }, // 218
	{ 0x00068, 605, "B", "parasol" }, // 219
	{ 0x06068, 607, "B", "peachtreeGC" }, // 220
	{ 0x000e8, 609, "BE", "penguin_l" }, // 221
	{ 0x001e8, 613, "BED", "penguin_m" }, // 222
	{ 0x001e8, 618, "BE3D", "penguin_s" }, // 223
	{ 0x01008, 625, "", "pile" }, // 224
	{ 0x000e8, 626, "BE", "pocha" }, // 225
	{ 0x000a8, 630, "E", "pochaMori" }, // 226
	{ 0x000e8, 633, "BE", "pochaYogan" }, // 227
	{ 0x001e8, 637, "BE2D", "poihana" }, // 228
	{ 0x00168, 643, "BD", "puchi_pakkun" }, // 229
	{ 0x00068, 646, "B", "pukupuku" }, // 230
	{ 0x00068, 648, "B", "pylon01" }, // 231
	{ 0x000e8, 650, "BE", "quicksand" }, // 232
	{ 0x00068, 654, "B", "r_parasol" }, // 233
	{ 0x00068, 656, "B", "ridgemii00" }, // 234
	{ 0x001e8, 658, "BED", "sanbo" }, // 235
	{ 0x00068, 663, "B", "seagull" }, // 236
	{ 0x00068, 665, "B", "skyship" }, // 237
	{ 0x00068, 667, "B", "spot" }, // 238
	{ 0x102e8, 669, "BEK", "starGate" }, // 239
	{ 0x00068, 674, "B", "sun" }, // 240
	{ 0x00068, 676, "B", "sunDS" }, // 241
	{ 0x000a8, 678, "E", "taimatsu" }, // 242
	{ 0x001e8, 681, "BED", "tree_cannon" }, // 243
	{ 0x000e8, 686, "BE", "truckChimSmk" }, // 244
	{ 0x000e8, 690, "BE", "truckChimSmkW" }, // 245
	{ 0x01008, 694, "", "v_stand1" }, // 246
	{ 0x01008, 695, "", "v_stand2" }, // 247
	{ 0x01008, 696, "", "venice_gondola" }, // 248
	{ 0x002e8, 697, "BEK", "venice_hasi" }, // 249
	{ 0x00068, 702, "B", "venice_nami" }, // 250
	{ 0x00268, 704, "BK", "venice_saku" }, // 251
	{ 0x01008, 707, "", "volfall" }, // 252
	{ 0x000a8, 708, "E", "volsmk" }, // 253
	{ 0x000e8, 711, "BE", "wanwan" }, // 254
	{ 0x00068, 715, "B", "windmill" }, // 255
	{ 0x060e8, 717, "BE", "woodbox" }, // 256
	{ 0x00e43, 721, "4BKP3x", "~course" }, // 257
	{ 0x00824, 731, "14x", "~posteffect" }, // 258
	{ 0x00820, 746, "x", "~useless" }, // 259
	{ 0x00a34, 748, "8x", "~~unknown" }, // 260

	{ -1, 0, "", "" } };

//
//--------------------------------------------------

const s16 DbFileRefGROUP[N_DB_FILE_REF_GROUP + 1] = {

	//   0, Alarm, N=2+1
	258, 259, -1,

	//   3, BGteresaSFC, N=1+1
	2, -1,

	//   5, BeltCrossing, N=1+1
	3, -1,

	//   7, BeltCurveA, N=1+1
	4, -1,

	//   9, BeltCurveB, N=0+1
	-1,

	//  10, BeltEasy, N=1+1
	5, -1,

	//  12, BlackHole, N=0+1
	-1,

	//  13, Block, N=3+1
	6, 260, 261, -1,

	//  17, BossField01_OBJ1, N=0+1
	-1,

	//  18, CarA1, N=1+1
	7, -1,

	//  20, CarA2, N=1+1
	8, -1,

	//  22, CarA3, N=1+1
	9, -1,

	//  24, CarB, N=1+1
	10, -1,

	//  26, Crane, N=2+1
	11, 12, -1,

	//  29, Crescent64, N=1+1
	13, -1,

	//  31, DKfalls, N=2+1
	262, 263, -1,

	//  34, DKmarutaGC, N=0+1
	-1,

	//  35, DKrockGC, N=3+1
	14, 264, 265, -1,

	//  39, DKropeGC, N=0+1
	-1,

	//  40, DKship64, N=3+1
	15, 266, 267, -1,

	//  44, DKtreeA64, N=1+1
	16, -1,

	//  46, DKtreeB64, N=1+1
	17, -1,

	//  48, DKturibashiGC, N=1+1
	18, -1,

	//  50, DemoCol, N=2+1
	19, 20, -1,

	//  53, DemoEf, N=2+1
	268, 269, -1,

	//  56, DemoJugemu, N=3+1
	21, 270, 271, -1,

	//  60, DonkyCannonGC, N=4+1
	22, 196, 272, 273, -1,

	//  65, DonkyCannon_wii, N=4+1
	23, 197, 274, 275, -1,

	//  70, EarthRing, N=1+1
	24, -1,

	//  72, EnvFire, N=2+1
	276, 277, -1,

	//  75, EnvKareha, N=2+1
	278, 279, -1,

	//  78, EnvKarehaUp, N=2+1
	280, 281, -1,

	//  81, EnvSnow, N=2+1
	282, 283, -1,

	//  84, Epropeller, N=3+1
	25, 284, 285, -1,

	//  88, FallBsA, N=2+1
	286, 287, -1,

	//  91, FallBsB, N=2+1
	288, 289, -1,

	//  94, FallBsC, N=0+1
	-1,

	//  95, Fall_MH, N=2+1
	290, 291, -1,

	//  98, Fall_Y, N=2+1
	292, 293, -1,

	// 101, FireSnake, N=3+1
	26, 294, 295, -1,

	// 105, FlagA1, N=1+1
	27, -1,

	// 107, FlagA2, N=1+1
	28, -1,

	// 109, FlagA3, N=1+1
	29, -1,

	// 111, FlagA4, N=0+1
	-1,

	// 112, FlagA5, N=1+1
	30, -1,

	// 114, FlagB1, N=1+1
	31, -1,

	// 116, FlagB2, N=1+1
	32, -1,

	// 118, FlagB3, N=1+1
	33, -1,

	// 120, FlagB4, N=1+1
	34, -1,

	// 122, FlamePole, N=4+1
	35, 36, 296, 297, -1,

	// 127, FlamePoleEff, N=0+1
	-1,

	// 128, FlamePole_v, N=3+1
	37, 298, 299, -1,

	// 132, FlamePole_v_big, N=2+1
	300, 301, -1,

	// 135, Flash_B, N=2+1
	302, 303, -1,

	// 138, Flash_L, N=2+1
	304, 305, -1,

	// 141, Flash_M, N=2+1
	306, 307, -1,

	// 144, Flash_S, N=2+1
	308, 309, -1,

	// 147, Flash_W, N=2+1
	310, 311, -1,

	// 150, HP_pipe, N=0+1
	-1,

	// 151, Hanabi, N=2+1
	312, 313, -1,

	// 154, HeyhoBallGBA, N=3+1
	38, 314, 315, -1,

	// 158, HeyhoShipGBA, N=1+1
	39, -1,

	// 160, HeyhoTreeGBA, N=1+1
	40, -1,

	// 162, InsekiA, N=1+1
	41, -1,

	// 164, InsekiB, N=1+1
	42, -1,

	// 166, K_bomb_car, N=3+1
	43, 316, 317, -1,

	// 170, K_bomb_car1, N=3+1
	44, 318, 319, -1,

	// 174, K_bomb_car_dummy, N=0+1
	-1,

	// 175, K_car_body, N=1+1
	45, -1,

	// 177, K_chairlift00, N=1+1
	46, -1,

	// 179, K_sticklift00, N=1+1
	47, -1,

	// 181, K_truck, N=1+1
	48, -1,

	// 183, Kamifubuki, N=2+1
	320, 321, -1,

	// 186, KmoonZ, N=1+1
	49, -1,

	// 188, KoopaFigure64, N=3+1
	50, 322, 323, -1,

	// 192, Ksticketc, N=1+1
	51, -1,

	// 194, MFaceBill, N=0+1
	-1,

	// 195, M_obj_jump, N=2+1
	52, 53, -1,

	// 198, M_obj_jump2, N=2+1
	54, 55, -1,

	// 201, M_obj_kanban, N=0+1
	-1,

	// 202, M_obj_kanban_y, N=0+1
	-1,

	// 203, M_obj_s_jump, N=2+1
	56, 57, -1,

	// 206, M_obj_s_jump2, N=2+1
	58, 59, -1,

	// 209, M_obj_start, N=0+1
	-1,

	// 210, MarioGo64, N=1+1
	60, -1,

	// 212, MashBalloonGC, N=1+1
	61, -1,

	// 214, Mdush, N=2+1
	62, 63, -1,

	// 217, MiiKanban, N=1+1
	64, -1,

	// 219, MiiObj01, N=1+1
	65, -1,

	// 221, MiiObj02, N=1+1
	66, -1,

	// 223, MiiObj03, N=1+1
	67, -1,

	// 225, MiiObjD01, N=1+1
	68, -1,

	// 227, MiiObjD02, N=1+1
	69, -1,

	// 229, MiiObjD03, N=1+1
	70, -1,

	// 231, MiiSighKino, N=1+1
	71, -1,

	// 233, MiiSignNoko, N=1+1
	72, -1,

	// 235, MiiSignWario, N=1+1
	73, -1,

	// 237, MiiSphinxY1, N=0+1
	-1,

	// 238, MiiSphinxY2, N=1+1
	74, -1,

	// 240, MiiStatue1, N=0+1
	-1,

	// 241, MiiStatueBD1, N=1+1
	75, -1,

	// 243, MiiStatueBL1, N=1+1
	76, -1,

	// 245, MiiStatueD1, N=1+1
	77, -1,

	// 247, MiiStatueD2, N=1+1
	78, -1,

	// 249, MiiStatueDK1, N=0+1
	-1,

	// 250, MiiStatueL1, N=1+1
	79, -1,

	// 252, MiiStatueL2, N=1+1
	80, -1,

	// 254, MiiStatueL3, N=1+1
	81, -1,

	// 256, MiiStatueM1, N=1+1
	82, -1,

	// 258, MiiStatueM2, N=1+1
	83, -1,

	// 260, MiiStatueP1, N=1+1
	84, -1,

	// 262, Mstand, N=0+1
	-1,

	// 263, PalmTree, N=1+1
	85, -1,

	// 265, PeachHunsuiGC, N=3+1
	86, 324, 325, -1,

	// 269, Piston, N=1+1
	87, -1,

	// 271, Press, N=3+1
	88, 326, 327, -1,

	// 275, Psea, N=1+1
	89, -1,

	// 277, RM_ring1, N=9+1
	90, 91, 92, 93, 94, 95, 96, 97, 98, -1,

	// 287, RhMiiKanban, N=1+1
	99, -1,

	// 289, SentakuDS, N=1+1
	100, -1,

	// 291, ShMiiObj01, N=1+1
	101, -1,

	// 293, ShMiiObj02, N=1+1
	102, -1,

	// 295, ShMiiObj03, N=1+1
	103, -1,

	// 297, SpaceSun, N=1+1
	104, -1,

	// 299, Spot64, N=1+1
	105, -1,

	// 301, StarRing, N=3+1
	106, 328, 329, -1,

	// 305, Steam, N=2+1
	330, 331, -1,

	// 308, TownBridgeDS, N=4+1
	107, 108, 109, 110, -1,

	// 313, TownTreeDS, N=1+1
	111, -1,

	// 315, TruckWagon, N=3+1
	112, 332, 333, -1,

	// 319, Twanwan, N=1+1
	113, -1,

	// 321, Twanwan_ue, N=0+1
	-1,

	// 322, TwistedWay, N=1+1
	114, -1,

	// 324, UtsuboDokan, N=1+1
	115, -1,

	// 326, VolcanoBall1, N=3+1
	116, 334, 335, -1,

	// 330, VolcanoPiece1, N=43+1
	117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
	136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154,
	155, 156, 157, 158, 159, -1,

	// 374, VolcanoRock1, N=3+1
	160, 161, 162, -1,

	// 378, WLarrowGC, N=1+1
	163, -1,

	// 380, WLdokanGC, N=2+1
	164, 165, -1,

	// 383, WLfirebarGC, N=1+1
	166, -1,

	// 385, WLscreenGC, N=1+1
	167, -1,

	// 387, WLwallGC, N=2+1
	168, 169, -1,

	// 390, ami, N=1+1
	170, -1,

	// 392, aurora, N=1+1
	171, -1,

	// 394, b_teresa, N=1+1
	172, -1,

	// 396, basabasa, N=1+1
	173, -1,

	// 398, bblock1, N=19+1
	174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192,
	-1,

	// 418, begoman_spike, N=3+1
	193, 336, 337, -1,

	// 422, bird, N=1+1
	194, -1,

	// 424, boble, N=3+1
	195, 338, 339, -1,

	// 428, bulldozer_left, N=2+1
	223, 224, -1,

	// 431, bulldozer_right, N=2+1
	225, 226, -1,

	// 434, car_body_dummy, N=0+1
	-1,

	// 435, casino_roulette, N=2+1
	227, 228, -1,

	// 438, castleballoon1, N=1+1
	229, -1,

	// 440, castleflower1, N=1+1
	230, -1,

	// 442, castletree1, N=1+1
	231, -1,

	// 444, castletree2, N=1+1
	232, -1,

	// 446, choropu, N=3+1
	233, 340, 341, -1,

	// 450, coin, N=3+1
	234, 342, 343, -1,

	// 454, cow, N=6+1
	198, 199, 200, 201, 202, 244, -1,

	// 461, crab, N=6+1
	203, 204, 205, 245, 344, 345, -1,

	// 468, cruiser, N=3+1
	246, 346, 347, -1,

	// 472, dc_pillar, N=4+1
	247, 248, 348, 349, -1,

	// 477, dc_sandcone, N=4+1
	250, 251, 350, 351, -1,

	// 482, dk_miiobj00, N=1+1
	252, -1,

	// 484, dkmonitor, N=1+1
	253, -1,

	// 486, dokan_sfc, N=1+1
	254, -1,

	// 488, donkytree1GC, N=1+1
	255, -1,

	// 490, donkytree2GC, N=1+1
	256, -1,

	// 492, dossun, N=3+1
	257, 352, 353, -1,

	// 496, dummy, N=0+1
	-1,

	// 497, entry, N=2+1
	354, 355, -1,

	// 500, escalator, N=2+1
	402, 403, -1,

	// 503, fks_screen_wii, N=1+1
	404, -1,

	// 505, flag, N=0+1
	-1,

	// 506, flagBlend, N=0+1
	-1,

	// 507, gardentreeDS, N=1+1
	405, -1,

	// 509, group_enemy_a, N=1+1
	406, -1,

	// 511, group_enemy_a_demo, N=1+1
	407, -1,

	// 513, group_enemy_b, N=1+1
	408, -1,

	// 515, group_enemy_c, N=1+1
	409, -1,

	// 517, group_enemy_d, N=0+1
	-1,

	// 518, group_enemy_e, N=1+1
	410, -1,

	// 520, group_enemy_f, N=1+1
	411, -1,

	// 522, group_monte_a, N=1+1
	412, -1,

	// 524, group_monte_a_demo, N=1+1
	413, -1,

	// 526, hanachan, N=5+1
	206, 207, 356, 357, 414, -1,

	// 532, heyho, N=1+1
	415, -1,

	// 534, heyho2, N=1+1
	416, -1,

	// 536, honeBall, N=3+1
	358, 359, 417, -1,

	// 540, ice, N=1+1
	418, -1,

	// 542, itembox, N=1+1
	420, -1,

	// 544, karehayama, N=3+1
	360, 361, 421, -1,

	// 548, kart_truck_dummy, N=0+1
	-1,

	// 549, kinoko, N=5+1
	422, 424, 425, 426, 427, -1,

	// 555, kinokoT1, N=1+1
	423, -1,

	// 557, kinokoT2, N=0+1
	-1,

	// 558, koopaBall, N=3+1
	362, 363, 428, -1,

	// 562, koopaFigure, N=3+1
	208, 209, 429, -1,

	// 566, koopaFirebar, N=1+1
	430, -1,

	// 568, kuribo, N=3+1
	210, 211, 431, -1,

	// 572, leaf_effect, N=2+1
	364, 365, -1,

	// 575, lensFX, N=0+1
	-1,

	// 576, light_house, N=1+1
	432, -1,

	// 578, mare_a, N=1+1
	434, -1,

	// 580, mare_b, N=1+1
	435, -1,

	// 582, mariotreeGC, N=1+1
	436, -1,

	// 584, mii_balloon, N=1+1
	437, -1,

	// 586, miiposter, N=1+1
	438, -1,

	// 588, monte_a, N=1+1
	439, -1,

	// 590, moray, N=1+1
	440, -1,

	// 592, npc_mii_a, N=0+1
	-1,

	// 593, npc_mii_b, N=0+1
	-1,

	// 594, npc_mii_c, N=0+1
	-1,

	// 595, obakeblockSFC, N=1+1
	441, -1,

	// 597, oilSFC, N=1+1
	442, -1,

	// 599, pakkun_dokan, N=1+1
	443, -1,

	// 601, pakkun_f, N=3+1
	212, 213, 444, -1,

	// 605, parasol, N=1+1
	445, -1,

	// 607, peachtreeGC, N=1+1
	446, -1,

	// 609, penguin_l, N=3+1
	366, 367, 447, -1,

	// 613, penguin_m, N=4+1
	214, 368, 369, 448, -1,

	// 618, penguin_s, N=6+1
	215, 216, 217, 370, 371, 449, -1,

	// 625, pile, N=0+1
	-1,

	// 626, pocha, N=3+1
	372, 373, 450, -1,

	// 630, pochaMori, N=2+1
	374, 375, -1,

	// 633, pochaYogan, N=3+1
	376, 377, 451, -1,

	// 637, poihana, N=5+1
	218, 219, 378, 379, 452, -1,

	// 643, puchi_pakkun, N=2+1
	220, 467, -1,

	// 646, pukupuku, N=1+1
	468, -1,

	// 648, pylon01, N=1+1
	469, -1,

	// 650, quicksand, N=3+1
	380, 381, 470, -1,

	// 654, r_parasol, N=1+1
	471, -1,

	// 656, ridgemii00, N=1+1
	472, -1,

	// 658, sanbo, N=4+1
	221, 382, 383, 474, -1,

	// 663, seagull, N=1+1
	475, -1,

	// 665, skyship, N=1+1
	476, -1,

	// 667, spot, N=1+1
	477, -1,

	// 669, starGate, N=4+1
	384, 385, 478, 479, -1,

	// 674, sun, N=1+1
	480, -1,

	// 676, sunDS, N=1+1
	481, -1,

	// 678, taimatsu, N=2+1
	386, 387, -1,

	// 681, tree_cannon, N=4+1
	222, 388, 389, 482, -1,

	// 686, truckChimSmk, N=3+1
	390, 391, 483, -1,

	// 690, truckChimSmkW, N=3+1
	392, 393, 484, -1,

	// 694, v_stand1, N=0+1
	-1,

	// 695, v_stand2, N=0+1
	-1,

	// 696, venice_gondola, N=0+1
	-1,

	// 697, venice_hasi, N=4+1
	394, 395, 485, 486, -1,

	// 702, venice_nami, N=1+1
	489, -1,

	// 704, venice_saku, N=2+1
	490, 491, -1,

	// 707, volfall, N=0+1
	-1,

	// 708, volsmk, N=2+1
	396, 397, -1,

	// 711, wanwan, N=3+1
	398, 399, 493, -1,

	// 715, windmill, N=1+1
	494, -1,

	// 717, woodbox, N=3+1
	400, 401, 495, -1,

	// 721, ~course, N=9+1
	236, 237, 238, 239, 240, 242, 243, 433, 492, -1,

	// 731, ~posteffect, N=14+1
	453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, -1,

	// 746, ~useless, N=1+1
	235, -1,

	// 748, ~~unknown, N=8+1
	0, 1, 241, 249, 419, 473, 487, 488, -1,

	// END
	-2
};

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////   E N D   //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
