import unreal

ASSETS = [
    "Body_Actual", "Underframe_Actual", "DriveSteerWheel_Actual", "LoadWheel_Actual",
    "MastOuter_Actual", "MastInner_Actual", "MastLift_Actual", "ReachCarriage_Actual",
    "ForkCarriage_Actual", "ForkLeft_Actual", "ForkRight_Actual",
]

for name in ASSETS:
    asset = unreal.load_asset("/Game/DigitalTwin/Art/R16HD/Imported/" + name)
    bounds = asset.get_bounds()
    size = bounds.box_extent * 2.0
    unreal.log("R16HD_BOUNDS {0}: cm=({1:.3f}, {2:.3f}, {3:.3f}) origin=({4:.3f}, {5:.3f}, {6:.3f})".format(
        name, size.x, size.y, size.z, bounds.origin.x, bounds.origin.y, bounds.origin.z))

for path in [
    "/Game/DigitalTwin/Art/R16HD/ScaleTest/ForkLeft_UEcmTest_ForkHeel.ForkLeft_UEcmTest_ForkHeel",
    "/Game/DigitalTwin/Art/R16HD/ScaleTest/ForkLeft_UEcmTest_ForkTine.ForkLeft_UEcmTest_ForkTine",
]:
    asset = unreal.load_asset(path)
    if asset:
        bounds = asset.get_bounds()
        size = bounds.box_extent * 2.0
        unreal.log("R16HD_SCALE_TEST {0}: cm=({1:.3f}, {2:.3f}, {3:.3f})".format(path, size.x, size.y, size.z))
