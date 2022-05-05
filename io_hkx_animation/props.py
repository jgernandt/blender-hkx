import bpy


AXES = [
    ("X", "X", "X axis"), 
    ("Y", "Y", "Y axis"), 
    ("Z", "Z", "Z axis"),
    ("-X", "-X", "Negative X axis"), 
    ("-Y", "-Y", "Negative Y axis"), 
    ("-Z", "-Z", "Negative Z axis"),
]


class ArmatureProperties(bpy.types.PropertyGroup):
    skeleton_path: bpy.props.StringProperty(
        name="Skeleton",
        description="Path to the HKX skeleton file",
        subtype='FILE_PATH')
    
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


class ArmaturePanel(bpy.types.Panel):
    """Panel for the Armature properties window"""
    bl_label = "HKX Export"
    bl_idname = "DATA_PT_io_hkx"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    
    @classmethod
    def poll(cls, context):
        return (context.object and context.object.type == 'ARMATURE')
    
    def draw(self, context):
        self.layout.prop(context.object.data.iohkx, "skeleton_path")
        
        #Is it stupid to show these here? 
        #Makes no sense to change them outside the import/export dialog.
        self.layout.prop(context.object.data.iohkx, "bone_forward")
        self.layout.prop(context.object.data.iohkx, "bone_up")


def register():
    bpy.utils.register_class(ArmatureProperties)
    bpy.utils.register_class(ArmaturePanel)
    bpy.types.Armature.iohkx = bpy.props.PointerProperty(type=ArmatureProperties)


def unregister():
    del bpy.types.Armature.iohkx
    bpy.utils.unregister_class(ArmaturePanel)
    bpy.utils.unregister_class(ArmatureProperties)
