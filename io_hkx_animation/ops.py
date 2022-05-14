import math
import os
import subprocess

import bpy
from bpy_extras.io_utils import axis_conversion
import bpy_extras;
import mathutils

from io_hkx_animation.ixml import DocumentInterface
from io_hkx_animation.ixml import ReferenceFrame
from io_hkx_animation.ixml import Track
from io_hkx_animation.prefs import EXEC_NAME
from io_hkx_animation.props import AXES

SAMPLING_RATE = 30

class HKXIO(bpy.types.Operator):
    
    length_scale: bpy.props.FloatProperty(
        name="Length scale",
        description="Scale factor for length units",
        default=10.0)
    
    primary_skeleton: bpy.props.StringProperty(
        name="Primary skeleton",
        description="Path to the HKX skeleton file",
    )
    
    secondary_skeleton: bpy.props.StringProperty(
        name="Secondary skeleton",
        description="Path to the skeleton of the second actor in a paired animation",
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
    
    def init_settings(self, context):
        #set skeleton path(s) to that of the active armature(s) (default if none)
        
        #primary is the active armature
        active = context.view_layer.objects.active
        
        if active and active.type == 'ARMATURE':
            self.primary_skeleton = active.data.iohkx.skeleton_path
            self.bone_forward = active.data.iohkx.bone_forward
            self.bone_up = active.data.iohkx.bone_up
        
        #default if none
        if self.primary_skeleton == "":
            self.primary_skeleton = context.preferences.addons[__package__].preferences.default_skeleton
        
        #secondary is the first non-active armature
        selected = context.view_layer.objects.selected
        for obj in selected:
            if obj.type == 'ARMATURE' and obj != active:
                self.secondary_skeleton = obj.data.iohkx.skeleton_path
                
                #need to decide how to store and expose the axis conventions!
                
                #self.bone_forward = obj.data.iohkx.bone_forward
                #self.bone_up = obj.data.iohkx.bone_up
                break
        
        #default if none 
        if self.secondary_skeleton == "":
            self.secondary_skeleton = context.preferences.addons[__package__].preferences.default_skeleton
    
    def axis_conversion(self, from_forward='Y', from_up='Z', to_forward='Y', to_up='Z'):
        #this throws if axes are invalid
        self.framerot = axis_conversion(
                from_forward=from_forward, 
                from_up=from_up, 
                to_forward=to_forward, 
                to_up=to_up).to_4x4()
        self.framerotinv = self.framerot.transposed()
    
    def get_converter(self, preferences):
        pref = preferences.addons[__package__].preferences.converter_tool
        exe = os.path.join(os.path.dirname(pref), EXEC_NAME)
        if not os.path.exists(exe):
            raise RuntimeError("Converter tool not found. Check your Addon Preferences.")
            
        return exe
    
    def get_selected(self, context):
        """Return all selected objects and all selected armatures"""
        selected = context.view_layer.objects.selected
        active = context.view_layer.objects.active
        
        #sort armatures so that the active one (if any) is first
        armatures = []
        if active and active.type == 'ARMATURE' and active.select_get():
            armatures.append(active)
        
        for obj in selected:
            if obj.type == 'ARMATURE' and obj != active:
                armatures.append(obj)
        
        return selected, armatures


class HKXImport(HKXIO, bpy_extras.io_utils.ImportHelper):
    bl_label = 'Import'
    bl_idname = 'io_hkx_animation.import'
    bl_description = "Import HKX animation"
    bl_options = {'UNDO'}
    
    filename_ext = ".hkx"
    
    filter_glob: bpy.props.StringProperty(
        default="*.hkx",
        options={'HIDDEN'})
    
    framerot: mathutils.Matrix
    framerotinv: mathutils.Matrix
    
    def invoke(self, context, event):
        #get the settings and forward to ImportHelper
        self.init_settings(context)
        return bpy_extras.io_utils.ImportHelper.invoke(self, context, event)
    
    def execute(self, context):
        try:
            #setup axis conversion
            self.axis_conversion(from_forward=self.bone_forward, from_up=self.bone_up)
            
            #Set fps to 30 (and warn if it wasn't)
            if context.scene.render.fps != SAMPLING_RATE:
                context.scene.render.fps = SAMPLING_RATE
                self.report({'WARNING'}, "Setting framerate to %s fps" % str(SAMPLING_RATE))
            
            #Look for the converter
            tool = self.get_converter(context.preferences)
            
            #Invoke the converter
            tmp_file = _tmpfilename(self.filepath, context.preferences)
            skels = '"%s" "%s"' % (self.primary_skeleton, self.secondary_skeleton)
            args = '"%s" unpack "%s" "%s" %s' % (tool, self.filepath, tmp_file, skels)
            res = subprocess.run(args)
            
            #throw if the converter returned non-zero
            res.check_returncode()
            
            #Load the xml
            doc = DocumentInterface.open(tmp_file)
            
            #Look up all selected armatures
            selected, armatures = self.get_selected(context)
            
            if len(armatures) == 0:
                #import armatures(s) from file
                
                #switch to object mode (not strictly required?)
                if bpy.ops.object.mode_set.poll():
                    bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
                
                #deselect all
                for obj in selected:
                    obj.select_set(False)
                    
                #create new armatures
                paths = [self.primary_skeleton, self.secondary_skeleton]
                armatures = [self.import_skeleton(i, context, p) for i, p in zip(doc.skeletons, paths)]
                if len(armatures) == 0:
                    raise RuntimeError("File contains no skeletons")
                
                #select and make active
                for arma in armatures:
                    arma.select_set(True)
                #If previously active object is excluded from the view layer, setting active fails.
                #No idea why. Fringe case, though. Move on.
                context.view_layer.objects.active = armatures[0]
                
            else:
                #number of selected armatures must match number of animations in the file
                n_anims = len(doc.animations)
                if len(armatures) != n_anims:
                    raise RuntimeError("Exactly %s or 0 Armatures must be selected" % (str(n_anims)))
                
                #One armature must be selected and active
                if not context.view_layer.objects.active in armatures:
                    raise RuntimeError("Primary Armature must be active")
            
            #this is now guaranteed to be one of our armatures
            active_obj = context.view_layer.objects.active
            
            #If there are more actions than armatures, duplicate active armature
            while len(doc.animations) > len(armatures):
                #append a duplicate of armature[0]
                armatures.append(active_obj.copy())
                #they can share data, right?
                #armatures[-1].data = armatures[-1].data.copy()
                context.scene.collection.objects.link(armatures[-1])
                
            #create new actions
            actions = [self.import_animation(i, arma) for i, arma in zip(doc.animations, armatures)]
            
            #add animation data if missing
            for arma in armatures:
                if not arma.animation_data:
                    arma.animation_data_create()
            
            #Then assign the actions
            for arma, acti in zip(armatures, actions):
                arma.animation_data.action = acti
            
        except Exception as e:
            self.report({'ERROR'}, str(e))
            return {'CANCELLED'}
        finally:
            #delete tmp file
            #os.remove(tmp_file)
            #(might also want to look at the tempfile module)
            pass
        
        self.report({'INFO'}, "Imported %s successfully" % self.filepath)
        return {'FINISHED'}
    
    def find_connected(self, bone, children):
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
    
    def import_animation(self, ianim, armature):
        
        #create a new action, named as file
        d, name = os.path.split(self.filepath)
        root, ext = os.path.splitext(name)
        action = bpy.data.actions.new(name=root)
        
        #look for bone name overrides
        overrides = {}
        for bone in armature.data.bones:
            if bone.iohkx.hkx_name != "":
                overrides[bone.iohkx.hkx_name] = bone.name
        
        #import the tracks
        for track in ianim.tracks():
            if track.datatype == Track.TRANSFORM:
                self.import_transform(track, action, overrides.get(track.name, track.name))
            elif track.datatype == Track.FLOAT:
                self.import_float(track, action)
        
        #import markers
        for annotation in ianim.annotations():
            marker = action.pose_markers.new(annotation.text)
            marker.frame = annotation.frame
        
        return action
    
    def import_bone(self, ibone, parent, armature):
        #add bone to armature
        bone = armature.data.edit_bones.new(ibone.name)
        bone.length = 1.0
        
        #transform
        loc, rot, scl = ibone.reference
        loc /= self.length_scale
        mat = mathutils.Matrix.LocRotScale(loc, rot, scl)
        if parent:
            bone.parent = parent
            #bone.matrix = parent.matrix @ mat
        #else:
            #bone.matrix = mat
        bone.matrix = mat @ self.framerot
        
        #recurse
        children = [self.import_bone(i, bone, armature) for i in ibone.bones()]
        
        #axis conversion (most efficient if done leaf->root)
        #(was, until we changed the input format)
        #bone.matrix = bone.matrix @ self.framerot
        
        #set length
        child, length = self.find_connected(bone, children)
        if child:
            bone.length = length
        #else leave it at 1
        
        return bone
    
    def import_float(self, itrack, action):
        #create f-curve
        f = action.fcurves.new('["%s"]' % itrack.name)
        
        #add keys
        for key in itrack.keys():
            f.keyframe_points.insert(key.frame, key.value, options={'FAST'})
        
        f.update()
    
    def import_skeleton(self, iskeleton, context, path):
        #create armature object
        data = bpy.data.armatures.new(name=iskeleton.name)
        armature = bpy.data.objects.new(iskeleton.name, data)
        context.scene.collection.objects.link(armature)
        
        #store our custom properties
        data.iohkx.skeleton_path = path
        data.iohkx.bone_forward = self.bone_forward
        data.iohkx.bone_up = self.bone_up
        
        #start editing armature
        context.view_layer.objects.active = armature
        bpy.ops.object.mode_set(mode='EDIT', toggle=False)
        
        #add bones
        for ibone in iskeleton.bones():
            self.import_bone(ibone, None, armature)
        
        #end edit
        bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
        
        #add float slots (custom props)
        for ifloat in iskeleton.floats():
            armature[ifloat.name] = ifloat.reference
        
        #display settings (optional?)
        data.display_type = 'STICK'
        data.show_axes = True
        
        return armature
    
    def import_transform(self, itrack, action, name):
        
        #create ActionGroup
        group = action.groups.new(name)
        
        #create new f-curves
        loc_x = action.fcurves.new('pose.bones["%s"].location' % name, index=0, action_group=name)
        loc_y = action.fcurves.new('pose.bones["%s"].location' % name, index=1, action_group=name)
        loc_z = action.fcurves.new('pose.bones["%s"].location' % name, index=2, action_group=name)
        
        rot_w = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % name, index=0, action_group=name)
        rot_x = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % name, index=1, action_group=name)
        rot_y = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % name, index=2, action_group=name)
        rot_z = action.fcurves.new('pose.bones["%s"].rotation_quaternion' % name, index=3, action_group=name)
        
        scl_x = action.fcurves.new('pose.bones["%s"].scale' % name, index=0, action_group=name)
        scl_y = action.fcurves.new('pose.bones["%s"].scale' % name, index=1, action_group=name)
        scl_z = action.fcurves.new('pose.bones["%s"].scale' % name, index=2, action_group=name)
        
        for key in itrack.keys():
            #do axis and scale conversion
            loc, rot, scl = key.value
            loc /= self.length_scale
            mat = mathutils.Matrix.LocRotScale(loc, rot, scl).to_4x4()
            mat = self.framerotinv @ mat @ self.framerot
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


FORMATS = [
    ("LE", "Skyrim", "32 bit format for the original Skyrim"), 
    ("SE", "Skyrim SE", "64 bit format for Skyrim Special Edition"), 
]


class HKXExport(HKXIO, bpy_extras.io_utils.ExportHelper):
    bl_label = 'Export'
    bl_idname = 'io_hkx_animation.export'
    bl_description = "Export animation as HKX"
    bl_options = {'UNDO'}
    
    filename_ext = ".hkx"
    
    filter_glob: bpy.props.StringProperty(
        default="*.hkx",
        options={'HIDDEN'},
    )
    
    blend_mode: bpy.props.BoolProperty(
        name="Additive",
        description="Store offsets instead of pose",
        default=False,
    )
    
    frame_interval: bpy.props.IntVectorProperty(
        name="Frame interval",
        description="First and last frame of the animation",
        size=2,
        min=0,
    )
    
    output_format: bpy.props.EnumProperty(
        items=FORMATS,
        name="Format",
        description="Format of the output HKX file",
        default='SE',
    )
    
    framerot: mathutils.Matrix
    framerotinv: mathutils.Matrix
    
    def invoke(self, context, event):
        #get the settings and forward to ImportHelper
        self.init_settings(context)
        self.frame_interval[0] = context.scene.frame_start
        self.frame_interval[1] = context.scene.frame_end
        return bpy_extras.io_utils.ExportHelper.invoke(self, context, event)

    def execute(self, context):
        try:
            #setup axis conversion
            self.axis_conversion(to_forward=self.bone_forward, to_up=self.bone_up)
            
            #Look for the converter
            tool = self.get_converter(context.preferences)
            
            #Look up all selected armatures
            selected, armatures = self.get_selected(context)
            active = context.view_layer.objects.active
            #fail if none
            if len(armatures) == 0 or not active in armatures:
                raise RuntimeError("Needs an active Armature")
            #fail if more than two
            if len(armatures) > 2:
                raise RuntimeError("Select at most two Armatures")
            
            #Look for the skeleton(s)
            if not os.path.exists(self.primary_skeleton):
                raise RuntimeError("Primary skeleton file not found")
            if len(armatures) > 1 and not os.path.exists(self.secondary_skeleton):
                raise RuntimeError("Secondary skeleton file not found")
            
            #Make sure we have frames to export
            if not self.frame_interval[1] > self.frame_interval[0]:
                raise RuntimeError("Frame interval is empty")
            
            #Save our custom properties
            for arma, path in zip(armatures, [self.primary_skeleton, self.secondary_skeleton]):
                arma.data.iohkx.skeleton_path = path
                arma.data.iohkx.bone_forward = self.bone_forward
                arma.data.iohkx.bone_up = self.bone_up
            
            #create a document
            doc = DocumentInterface.create()
            
            #determine our sampling parameters
            self.framestep = context.scene.render.fps / SAMPLING_RATE
            framesteps = self.frame_interval[1] - self.frame_interval[0]
        
            #if framerate is not 30 fps, we sample at nearest possible rate and warn
            if context.scene.render.fps != SAMPLING_RATE:
                framesteps = int(round(framesteps / self.framestep))
                self.report({'WARNING'}, "Sampling animation at %s fps" % str(SAMPLING_RATE))
            
            self.frames = framesteps + 1
            
            #add frame, framerate, blend mode
            doc.set_frames(self.frames)
            doc.set_framerate(SAMPLING_RATE)
            doc.set_additive(self.blend_mode)
            
            #add animations
            for armature in armatures:
                context.view_layer.objects.active = armature
                self.export_animation(doc, context)
                
            #restore active state
            context.view_layer.objects.active = active
            
            if len(doc.animations) != 0:
                #write xml
                tmp_file = _tmpfilename(self.filepath, context.preferences)
                doc.save(tmp_file)
                
                #invoke converter
                if len(doc.animations) == 1:
                    skels = '"%s"' % (self.primary_skeleton)
                else:
                    skels = '"%s" "%s"' % (self.primary_skeleton, self.secondary_skeleton)
                
                if self.output_format == 'LE':
                    fmt = "WIN32"
                else:
                    fmt = "AMD64"
                
                args = '"%s" pack %s "%s" "%s" %s' % (tool, fmt, tmp_file, self.filepath, skels)
                
                print(args)
                res = subprocess.run(args)
                
                #throw if the converter returned non-zero
                res.check_returncode()
            
        except Exception as e:
            self.report({'ERROR'}, str(e))
            return {'CANCELLED'}
        finally:
            #delete tmp file
            #os.remove(tmp_file)
            #(might also want to look at the tempfile module)
            pass
        
        self.report({'INFO'}, "Exported %s successfully" % self.filepath)
        return {'FINISHED'}
    
    def export_animation(self, document, context):
        
        armature = context.view_layer.objects.active
        
        #abort if no bones are selected
        pbones = context.selected_pose_bones_from_active_object
        if not pbones or len(pbones) == 0:
            self.report({'WARNING'}, "No bones selected in %s, ignoring" % armature.name)
            return
        
        #name of animation = index
        ianim = document.add_animation(str(len(document.animations)))
        #name of skeleton = object name
        ianim.set_skeleton_name(armature.data.iohkx.skeleton_path)
        #reference frame = object
        ianim.set_reference_frame(ReferenceFrame.OBJECT)
        
        #use the name override (if any) as track name
        override = lambda pbone: pbone.bone.iohkx.hkx_name if pbone.bone.iohkx.hkx_name != "" else pbone.name
        tracks = [ianim.add_transform_track(override(bone)) for bone in pbones]
        
        #we'll export only the properties that are keyframed in the current action
        slots = []
        action = armature.animation_data.action if armature.animation_data else None
        if action:
            for prop in armature.keys():
                #save this property if it has an FCurve
                if action.fcurves.find('["%s"]' % prop):
                    slots.append(ianim.add_float_track(prop))
        
        #loop over frames, add key for each track
        current_frame = context.scene.frame_current
        for i in range(self.frames):
            #set current frame (and subframe, if appropriate)
            if context.scene.render.fps == SAMPLING_RATE:
                context.scene.frame_set(self.frame_interval[0] + i)
            else:
                subframe, frame = math.modf(self.frame_interval[0] + i * self.framestep)
                context.scene.frame_set(int(frame), subframe=subframe)
            
            for bone, track in zip(pbones, tracks):
                #read current object-space transform
                loc, rot, scl = bone.matrix.decompose()
                
                #rotate to output frame
                mat = mathutils.Matrix.LocRotScale(loc, rot, scl).to_4x4() @ self.framerot
                
                #Transform to parent-bone space
                #do this in the converter instead, less double transforming
                #if bone.parent:
                #    try:
                #        imat = (bone.parent.matrix @ self.framerot).inverted()
                #    except:
                #        raise RuntimeError("Scale must not be zero")
                #    loc, rot, scl = (imat @ mat).decompose()
                #else:
                #    loc, rot, scl = mat.decompose()
                
                loc, rot, scl = mat.decompose()
                
                #rescale length
                loc *= self.length_scale
                
                #add key
                key = track.add_key(i)
                key.set_value(loc, rot, scl)
            
            for slot in slots:
                print("Adding key: " + str(i))
                key = slot.add_key(i)
                print("Slot name: " + slot.name)
                print("get rturned: " + str(armature.get(slot.name)))
                key.set_value(armature.get(slot.name))
        
        #restore state
        context.scene.frame_set(current_frame)
        
        #Add annotations from pose markers
        if armature.animation_data and armature.animation_data.action:
            for marker in armature.animation_data.action.pose_markers:
                if marker.frame >= self.frame_interval[0] and marker.frame <= self.frame_interval[1]:
                    #count from frame_interval[0]
                    i = (marker.frame - self.frame_interval[0]) / self.framestep + 1
                    ianim.add_annotation(i, marker.name)


def _tmpfilename(file_name, preferences):
    #read dir from preferences
    loc = preferences.addons[__package__].preferences.temp_location
    
    #Use converter dir if no temp location is set
    if loc == "":
        loc = preferences.addons[__package__].preferences.converter_tool
    
    root, ext = os.path.splitext(os.path.basename(file_name))
    #return loc/fileroot.tmp
    return os.path.join(loc, root) + ".tmp"


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

