import math
import os
import subprocess

import bpy
from bpy_extras.io_utils import axis_conversion
import bpy_extras;
import mathutils

from io_hkx_animation.ixml import DocumentInterface
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
    
    def initsettings(self, context):
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
    
    def axisconversion(self, from_forward='Y', from_up='Z', to_forward='Y', to_up='Z'):
        #this throws if axes are invalid
        self.framerot = axis_conversion(
                from_forward=from_forward, 
                from_up=from_up, 
                to_forward=to_forward, 
                to_up=to_up).to_4x4()
        self.framerotinv = self.framerot.transposed()
    
    def getconverter(self, preferences):
        pref = preferences.addons[__package__].preferences.converter_tool
        exe = os.path.join(os.path.dirname(pref), EXEC_NAME)
        if not os.path.exists(exe):
            raise RuntimeError("Converter tool not found. Check your Addon Preferences.")
            
        return exe
    
    def getselected(self, context):
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
        self.initsettings(context)
        return bpy_extras.io_utils.ImportHelper.invoke(self, context, event)
    
    def execute(self, context):
        try:
            #setup axis conversion
            self.axisconversion(from_forward=self.bone_forward, from_up=self.bone_up)
            
            #Set fps to 30 (and warn if it wasn't)
            if context.scene.render.fps != SAMPLING_RATE:
                context.scene.render.fps = SAMPLING_RATE
                self.report({'WARNING'}, "Setting framerate to %s fps" % str(SAMPLING_RATE))
            
            #Look for the converter
            tool = self.getconverter(context.preferences)
            
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
            selected, armatures = self.getselected(context)
            
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
                armatures = [self.importskeleton(i, context, p) for i, p in zip(doc.skeletons, paths)]
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
                
            #create new actions
            actions = [self.importanimation(i, context) for i in doc.animations]
            
            #If there are more actions than armatures, duplicate active armature
            while len(actions) > len(armatures):
                #append a duplicate of armature[0]
                armatures.append(active_obj.copy())
                #they can share data, right?
                #armatures[-1].data = armatures[-1].data.copy()
                context.scene.collection.objects.link(armatures[-1])
            
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
        
        return {'FINISHED'}
    
    def importfloat(self, itrack, action):
        pass
    
    def importtransform(self, itrack, action):
        
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
    
    def importanimation(self, ianim, context):
        
        #create a new action, named as file
        d, name = os.path.split(self.filepath)
        root, ext = os.path.splitext(name)
        action = bpy.data.actions.new(name=root)
        #armature.animation_data.action = action
        
        #import the tracks
        for track in ianim.tracks():
            if track.datatype == Track.TRANSFORM:
                self.importtransform(track, action)
            elif track.datatype == Track.FLOAT:
                self.importfloat(track, action)
        
        #import markers
        for annotation in ianim.annotations():
            print("Marker %s at %s" % (annotation.text, str(annotation.frame)))
            marker = action.pose_markers.new(annotation.text)
            marker.frame = annotation.frame
        
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
        bone.matrix = mat @ self.framerot
        
        #recurse
        children = [self.importbone(i, bone, armature) for i in ibone.bones()]
        
        #axis conversion (most efficient if done leaf->root)
        #(was, until we changed the input format)
        #bone.matrix = bone.matrix @ self.framerot
        
        #set length
        child, length = self.findconnected(bone, children)
        if child:
            bone.length = length
        #else leave it at 1
        
        return bone
    
    def importskeleton(self, iskeleton, context, path):
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
            self.importbone(ibone, None, armature)
        
        #end edit
        bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
        
        #display settings (optional?)
        data.display_type = 'STICK'
        data.show_axes = True
        
        return armature


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
        self.initsettings(context)
        self.frame_interval[0] = context.scene.frame_start
        self.frame_interval[1] = context.scene.frame_end
        return bpy_extras.io_utils.ExportHelper.invoke(self, context, event)

    def execute(self, context):
        try:
            #setup axis conversion
            self.axisconversion(to_forward=self.bone_forward, to_up=self.bone_up)
            
            #Look for the converter
            tool = self.getconverter(context.preferences)
            
            #Look up all selected armatures
            selected, armatures = self.getselected(context)
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
                self.exportanimation(doc, armature, context)
                
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
                
                #debug
                fmt = "XML"
                
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
    
    def exportanimation(self, document, armature, context):
        
        #activate this armature
        context.view_layer.objects.active = armature
        
        #abort if no bones are selected
        bones = context.selected_pose_bones_from_active_object
        if not bones or len(bones) == 0:
            self.report({'WARNING'}, "No bones selected in %s, ignoring" % armature.name)
            return
        
        #name of animation = index
        ianim = document.add_animation(str(len(document.animations)))
        #name of skeleton = object name
        ianim.set_skeleton_name(armature.data.iohkx.skeleton_path)
        
        tracks = [ianim.add_transform_track(bone.name) for bone in bones]
        
        #loop over frames, add key for each track
        current_frame = context.scene.frame_current
        for i in range(self.frames):
            #set current frame (and subframe, if appropriate)
            if context.scene.render.fps == SAMPLING_RATE:
                context.scene.frame_set(self.frame_interval[0] + i)
            else:
                subframe, frame = math.modf(self.frame_interval[0] + i * self.framestep)
                context.scene.frame_set(int(frame), subframe=subframe)
            
            for bone, track in zip(bones, tracks):
                #pull current transform
                loc, rot, scl = bone.matrix.decompose()
                
                #coordinate transforms
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
                
                loc *= self.length_scale
                
                #add key
                key = track.add_key(i)
                key.set_value(loc, rot, scl)
        
        #restore state
        context.scene.frame_set(current_frame)
        
        #Add annotations from pose markers
        if armature.animation_data.action:
            for marker in armature.animation_data.action.pose_markers:
                if marker.frame >= self.frame_interval[0] and marker.frame <= self.frame_interval[1]:
                    #count from frame_interval[0]
                    i = marker.frame - self.frame_interval[0] + 1
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

