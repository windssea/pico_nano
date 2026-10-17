/* TTF与灰阶模块。/ TTF and grayscale modules. */
FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)
/* OTF(CFF轮廓)：CFF驱动及其PostScript辅助/提示模块。/ OTF (CFF outlines): the CFF driver with its PostScript auxiliary and hinter modules. */
FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)
FT_USE_MODULE(FT_Module_Class, psaux_module_class)
FT_USE_MODULE(FT_Module_Class, pshinter_module_class)
FT_USE_MODULE(FT_Module_Class, sfnt_module_class)
FT_USE_MODULE(FT_Module_Class, psnames_module_class)
FT_USE_MODULE(FT_Renderer_Class, ft_smooth_renderer_class)
FT_USE_MODULE(FT_Renderer_Class, ft_raster1_renderer_class)
