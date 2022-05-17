# blender-hkx
A Blender addon to import/export Skyrim animations (HKX files)

## About
This is a complete solution for importing and exporting Skyrim animations to and from Blender. It can handle:
- Regular animations
- Paired animations
- Additive animations
- Annotations
- Float tracks

The addon depends on an external program to pack and unpack the compressed Havok animations. Place this program anywhere on your computer and enter its location in the Addon preferences in Blender.

Both importing and exporting requires a Havok skeleton file, since Havok animation files only contain an anonymous list of bones. The skeleton is required to make sure the bones are read or written in the right order.

The converter can only read 32 bit HKX files (original Skyrim), but it can write either 32 or 64 bit files (Skyrim SE). This applies to both skeletons and animations.

Havok expects animations to be sampled at 30 fps. If you have created an animation with a different frame rate, it may not turn out exactly the same as it looks in Blender.

## Import
Located in File>Import.

If an Armature object is selected, the animation will be added to it. Otherwise, an Armature object will be imported along with the animation.

To import a paired animation, you need the individual skeleton files (not some combined paired skeleton!). Like single animations, paired animations will be added to existing Armature objects if they are selected. Otherwise, they will be imported along with the animation.

Any annotations in the animation will be imported as Pose Markers (can be enabled in the Action Editor).

Any float tracks in the animation will be imported as custom properties on the Armature object.

## Export
Located in File>Export.

Select one or two armatures, enter Pose mode and select all the bones that should be exported. The exporter uses the final visible pose (i. e. after constraints and drivers).

Paired animations should be made with two separate Armature objects, not a combined one. They will be merged properly by the converter.

If the 'Additive' option is toggled on, bone transforms will be saved as offsets from the rest pose instead of as a final pose. This is typically only used for specific addon animations.

Any Pose Markers in the current Action of the selected Armature object will be exported as annotations.

Any custom properties on the Armature object will be exported as float tracks, if they are keyframed in the current Action.

## Build instructions
To build the converter tool from source, you need:
- Visual Studio 2019
- Havok SDK 2010 2.0-r1
- pugixml
