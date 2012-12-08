from distutils.core import setup, Extension

module1 = Extension('elfdata',
                    include_dirs = ['/usr/include/elfutils','.'],
                    libraries = ['elf','asm','dw','ebl'],
                    #library_dirs = ['/usr/lib/mysql','/usr/lib64/mysql'],
                    sources = ['elfdata.c'],
                    extra_compile_args=['-std=c99'])

setup (name = 'elfdata',
       version = '0.3',
       description = 'elfdata from elf files',
       author = 'Kushal Das',
       author_email = 'kushaldas@gmail.com',
       url = 'https://github.com/kushaldas/elfdata',
       ext_modules = [module1])
