import math
import os

import bpy
from bpy_extras.io_utils import axis_conversion
import mathutils

from io_hkx_animation.ixml import DocumentInterface
from io_hkx_animation.ixml import Track
from io_hkx_animation.prefs import EXEC_NAME
from io_hkx_animation.props import AXES

import logging
log = logging.getLogger(__name__)
log.setLevel(0)

import bpy_extras;

SAMPLING_RATE = 30

class HKXIO(bpy.types.Operator):
    
    length_scale: bpy.props.FloatProperty(
        name="Length scale",
        description="Scale factor for length units",
        default=10.0)
    
    #axis mapping

class HKXImport(HKXIO, bpy_extras.io_utils.ImportHelper):
    bl_label = 'Import'
    bl_idname = 'io_hkx_animation.import'
    bl_description = "Import HKX animation"
    bl_options = {'UNDO'}
    
    filename_ext = ".hkx"
    
    filter_glob: bpy.props.StringProperty(
        default="*.hkx",
        options={'HIDDEN'})
    
    skeleton_path: bpy.props.StringProperty(
        name="Skeleton",
        description="Path to the HKX skeleton file",
    )
    
    bone_forward: bpy.props.EnumProperty(
        items=AXES,
        name="Forward axis",
        description="This axis will be mapped to Blender's Y axis",
        default='Y',
        #update=callbackfcn
    )
    
    bone_up: bpy.props.EnumProperty(
        items=AXES,
        name="Up axis",
        description="This axis will be mapped to Blender's Z axis",
        default='Z',
        #update=callbackfcn
    )
    
    hktoblender: mathutils.Matrix
    hktoblenderinv : mathutils.Matrix
    
    def invoke(self, context, event):
        #set skeleton_path to the path of the active armature (default if none)
        active_obj = context.view_layer.objects.active
        if active_obj and active_obj.type == 'ARMATURE':
            self.skeleton_path = active_obj.data.iohkx.skeleton_path
            self.bone_forward = active_obj.data.iohkx.bone_forward
            self.bone_up = active_obj.data.iohkx.bone_up
        if self.skeleton_path == "":
            self.skeleton_path = context.preferences.addons[__package__].preferences.default_skeleton
        
        #forward to ImportHelper
        return bpy_extras.io_utils.ImportHelper.invoke(self, context, event)
    
    def execute(self, context):
        try:
            #setup axis conversion
            #this throws if axes are invalid
            self.hktoblender = axis_conversion(
                from_forward=self.bone_forward, from_up=self.bone_up).to_4x4()
            self.hktoblenderinv = self.hktoblender.transposed()
            
            #Set fps to 30 (and warn if it wasn't)
            if context.scene.render.fps != SAMPLING_RATE:
                context.scene.render.fps = SAMPLING_RATE
                self.report({'WARNING'}, "Setting framerate to %s fps" % str(SAMPLING_RATE))
            
            #Look for the converter
            pref = context.preferences.addons[__package__].preferences.converter_tool
            exe = os.path.join(os.path.dirname(pref), EXEC_NAME)
            if not os.path.exists(exe):
                raise RuntimeError("Converter tool not found. Check your Addon Preferences.")
            
            #Invoke the converter
            
            #Load the xml
            doc = DocumentInterface(_tmpfilename(self.filepath))
            
            #If nothing is selected or active object is not an Armature, create a new one
            selected = context.view_layer.objects.selected
            active_obj = context.view_layer.objects.active
            #In case setting active fails (see below)
            armature = None
            if len(selected) == 0 or not active_obj or active_obj.type != 'ARMATURE':
                #switch to object mode (not strictly required?)
                if bpy.ops.object.mode_set.poll():
                    bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
                
                #deselect all
                for obj in selected:
                    obj.select_set(False)
                    
                #create new armatures
                armatures = [self.importskeleton(i, context) for i in doc.skeletons()]
                if len(armatures) == 0:
                    raise RuntimeError("File contains no skeletons")
                
                #select and make active
                armature = armatures[0]
                armature.select_set(True)
                #If previously active object is excluded from the view layer, setting active fails.
                #No idea why. Fringe case, though. Move on.
                context.view_layer.objects.active = armature
                
            else:
                armature = active_obj
            
            #add animation data if missing
            if not armature.animation_data:
                armature.animation_data_create()
                
            #create new actions
            actions = [self.importanimation(i, context) for i in doc.animations()]
            
            
            
            
        except Exception as e:
            self.report({'ERROR'}, str(e))
        finally:
            #delete tmp file
            pass
        
        return {'FINISHED'}
    
    def importfloat(self, itrack, action, armature):
        pass
    
    def importtransform(self, itrack, action, armature):
        bone = armature.pose.bones[itrack.name]
        assert bone, "unknown track encountered"
        
        #create ActionGroup
        group = action.groups.new(itrack.name)
        
        #create new f-curves
        loc_x = action.fcurves.new('pose.bones["%s"].location' % itrack.name, index=0, action_group=itrack.name)
        loc_y = action.fcurves.new('pose.bones["%s"].location' % itrack.name, index=1, action_group=itrack.name)
        loc_z = action.fcurves.new('pose.bones["%s"].location' % itrack.name, index=2, action_group=itrack.name)
        
        rot_w = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % itrack.name, index=0, action_group=itrack.name)
        rot_x = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % itrack.name, index=1, action_group=itrack.name)
        rot_y = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % itrack.name, index=2, action_group=itrack.name)
        rot_z = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % itrack.name, index=3, action_group=itrack.name)
        
        scl_x = action.fcurves.new('pose.bones["%s"].scale' % itrack.name, index=0, action_group=itrack.name)
        scl_y = action.fcurves.new('pose.bones["%s"].scale' % itrack.name, index=1, action_group=itrack.name)
        scl_z = action.fcurves.new('pose.bones["%s"].scale' % itrack.name, index=2, action_group=itrack.name)
        
        for key in itrack.keys():
            #do axis and scale conversion
            loc, rot, scl = key.value
            loc /= self.length_scale
            mat = mathutils.Matrix.LocRotScale(loc, rot, scl).to_4x4()
            mat = self.hktoblenderinv @ mat @ self.hktoblender
            loc, rot, scl = mat.decompose()
            
            #insert keyframes
            loc_x.keyframe_points.insert(key.frame, loc[0], options={'FAST'})
            loc_y.keyframe_points.insert(key.frame, loc[1], options={'FAST'})
            loc_z.keyframe_points.insert(key.frame, loc[2], options={'FAST'})
            
            rot_w.keyframe_points.insert(key.frame, rot[0], options={'FAST'})
            rot_x.keyframe_points.insert(key.frame, rot[1], options={'FAST'})
            rot_y.keyframe_points.insert(key.frame, rot[2], options={'FAST'})
            rot_z.keyframe_points.insert(key.frame, rot[3], options={'FAST'})
            
            scl_x.keyframe_points.insert(key.frame, scl[0], options={'FAST'})
            scl_y.keyframe_points.insert(key.frame, scl[1], options={'FAST'})
            scl_z.keyframe_points.insert(key.frame, scl[2], options={'FAST'})
        
        loc_x.update()
        loc_y.update()
        loc_z.update()
        
        rot_w.update()
        rot_x.update()
        rot_y.update()
        rot_z.update()
        
        scl_x.update()
        scl_y.update()
        scl_z.update()
    
    def importanimation(self, ianim, context):
        assert ianim.framerate == 30, "unexpected framrate"
        #We expect the only selected object(s) to be the armature(s) that we created.
        armature = context.view_layer.objects.active
        
        #create a new action, named as file
        d, name = os.path.split(self.filepath)
        root, ext = os.path.splitext(name)
        action = bpy.data.actions.new(name=root)
        armature.animation_data.action = action
        
        #import the tracks
        for track in ianim.tracks():
            if track.datatype == Track.TRANSFORM:
                self.importtransform(track, action, armature)
            elif track.datatype == Track.FLOAT:
                self.importfloat(track, action, armature)
        
        return action
    
    def findconnected(self, bone, children):
        """Find the child (if any) that continues our bone chain, and the distance to it."""
        #A connected child should be located on our positive y axis, to within roundoff error.
        epsilon = 1e-5
        our_loc = bone.matrix.to_translation()
        for child in children:
            separation = child.matrix.to_translation() - our_loc
            #reject bones that are too close to us
            if separation.length > epsilon:
                #For separation to be parallel to our y axis, the scalar vector rejection
                #of separation from y should be less than epsilon.
                #It might be simpler to just check the angle between separation and y,
                #but that makes the error threshold slightly more complicated instead.
                assert abs(1.0 - bone.y_axis.length) < epsilon, "invalid assumption"
                scalar_projection = separation.dot(bone.y_axis)
                #reject bones on the negative side
                if scalar_projection >= 0.0:
                    rejection = separation - scalar_projection * bone.y_axis
                    if rejection.length < epsilon:
                        return child, separation.length
        
        #we found no connected child
        return None, None
    
    def importbone(self, ibone, parent, armature):
        log.debug("Importing bone " + ibone.name)
        #add bone to armature
        bone = armature.data.edit_bones.new(ibone.name)
        bone.length = 1.0
        
        #transform
        loc, rot, scl = ibone.refpose()
        loc /= self.length_scale
        mat = mathutils.Matrix.LocRotScale(loc, rot, scl)
        if parent:
            bone.parent = parent
            #bone.matrix = parent.matrix @ mat
        #else:
            #bone.matrix = mat
        bone.matrix = mat
        
        #recurse
        children = [self.importbone(i, bone, armature) for i in ibone.bones()]
        
        #axis conversion (most efficient if done leaf->root)
        bone.matrix = bone.matrix @ self.hktoblender
        
        #set length
        child, length = self.findconnected(bone, children)
        if child:
            bone.length = length
        #else leave it at 1
        
        return bone
    
    def importskeleton(self, iskeleton, context):
        #create armature object
        data = bpy.data.armatures.new(name=iskeleton.name)
        armature = bpy.data.objects.new(iskeleton.name, data)
        context.scene.collection.objects.link(armature)
        
        #store our custom properties
        data.iohkx.skeleton_path = self.skeleton_path
        data.iohkx.bone_forward = self.bone_forward
        data.iohkx.bone_up = self.bone_up
        
        #start editing armature
        context.view_layer.objects.active = armature
        bpy.ops.object.mode_set(mode='EDIT', toggle=False)
        
        #add bones
        for ibone in iskeleton.bones():
            self.importbone(ibone, None, armature)
        
        #end edit
        bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
        
        #display settings (optional?)
        data.display_type = 'STICK'
        data.show_axes = True
        
        return armature


def _tmpfilename(file_name):
    root, ext = os.path.splitext(file_name)
    return root + "-tmp.xml"
    #return root + "-163498.xml"

_boneNameMap = dict({"WEAPON": "Weapon", "SHIELD": "Shield", "QUIVER": "Quiver"})


def _mapname(name):
    #Some names differ between nifs and hkxs. 
    #Specifically Weapon, Shield and Quiver, which are all caps in nif.
    #We want to output the hkx name.
    
    #This could be made configurable, similar to what niftools do.
    
    if name in _boneNameMap:
        return _boneNameMap[name]
    else:
        return name


class HKXExport(HKXIO, bpy_extras.io_utils.ExportHelper):
    bl_label = 'Export'
    bl_idname = 'io_hkx.export'
    bl_description = "Export animation as HKX"
    
    filename_ext = ".hkx"
    
    filter_glob: bpy.props.StringProperty(
        default="*.hkx",
        options={'HIDDEN'})
    
    hkx_blend_hint_additive: bpy.props.BoolProperty(
        name="Additive",
        description="Store offsets instead of pose",
        default=False)

    def execute(self, context):
        try:
            #Check that we can find the exe
            
            
            #get the active object
            #   if none, throw
            selected = bpy.context.view_layer.objects.selected
            active_obj = bpy.context.view_layer.objects.active
            if len(selected) == 0 or not active_obj or active_obj.type != 'ARMATURE':
                raise RuntimeError("No active Armature")
                
            #get the active Action of the active object
            #   if none, throw
            #action = None
            #if active_obj.animation_data:
            #    action = active_obj.animation_data.action
            
            #if not action:
            #    raise RuntimeError("Active object has no active Action")
            
            #make sure that the skeleton file exists
            skeleton_file = bpy.path.abspath(active_obj.data.iohkx.skeleton_path)
            if not os.path.exists(skeleton_file):
                raise RuntimeError("Skeleton file %s not found" % skeleton_file)
            
            #write tmp file
            tmp_file = _tmpfilename(self.filepath)
            with open(tmp_file, mode='w') as f:
                if self.hkx_blend_hint_additive:
                    gen = None
                else:
                    gen = ActionGenerator(f)
                gen.armature = active_obj
                gen.scene = context.scene
                gen.lengthScale = self.hkx_scale_factor
                gen.generate(self)
            
            #invoke exe
            
            self.report({'INFO'}, "Exported %s successfully" % self.filepath)
            
        except OSError as e:
            self.report({'ERROR'}, str(e))
        except RuntimeError as e:
            self.report({'ERROR'}, str(e))
        finally:
            #delete tmp file
            pass
        
        return {'FINISHED'}
    

def exportop(self, context):
    self.layout.operator(HKXExport.bl_idname, text="Havok Animation (.hkx)")
    
def importop(self, context):
    self.layout.operator(HKXImport.bl_idname, text="Havok Animation (.hkx)")

def register():
    bpy.utils.register_class(HKXImport)
    bpy.utils.register_class(HKXExport)
    bpy.types.TOPBAR_MT_file_import.append(importop)
    bpy.types.TOPBAR_MT_file_export.append(exportop)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(exportop)
    bpy.types.TOPBAR_MT_file_import.remove(importop)
    bpy.utils.unregister_class(HKXExport)
    bpy.utils.unregister_class(HKXImport)

