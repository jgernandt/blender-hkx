import bpy

EXEC_NAME = "blender-hkx.exe"

class HKXAddonPreferences(bpy.types.AddonPreferences):
    bl_idname = __package__
    
    converter_tool: bpy.props.StringProperty(
        name="Converter location",
        subtype='DIR_PATH',
        description="The location of " + EXEC_NAME,
    )
    
    default_skeleton: bpy.props.StringProperty(
        name="Default skeleton",
        subtype='FILE_PATH',
        description="Path to the default HKX skeleton file",
    )
    
    temp_location: bpy.props.StringProperty(
        name="Temporary folder",
        subtype='DIR_PATH',
        description="Location to store temporary files",
    )
    
    def draw(self, context):
        self.layout.prop(self, "converter_tool")
        self.layout.prop(self, "temp_location")
        self.layout.prop(self, "default_skeleton")

def register():
    bpy.utils.register_class(HKXAddonPreferences)

def unregister():
    bpy.utils.unregister_class(HKXAddonPreferences)
