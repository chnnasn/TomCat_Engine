import os
import subprocess
import sys
import zipfile
import shutil

# 首先检查和安装必要的包
import CheckPython
CheckPython.ValidatePackages()

# 现在再导入 Utils，确保所有依赖都已安装
import Utils

# Check if premake5 exists, if not download it
premake_dir = "vendor/premake/bin"
premake_exe = os.path.join(premake_dir, "premake5.exe")

if not os.path.exists(premake_exe):
    print("Premake5 not found. Downloading...")
    
    # Create directory if it doesn't exist
    os.makedirs(premake_dir, exist_ok=True)
    
    # Download premake5
    premake_url = "https://github.com/premake/premake-core/releases/download/v5.0.0-beta7/premake-5.0.0-beta7-windows.zip"
    zip_path = os.path.join(premake_dir, "premake.zip")
    
    Utils.DownloadFile(premake_url, zip_path)
    
    # Extract the zip file
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(premake_dir)
    
    # Remove the zip file
    os.remove(zip_path)
    
    print("Premake5 downloaded and extracted successfully.")

# 移动 vendor 文件夹到上一级目录（合并）
current_dir = os.path.dirname(os.path.abspath(__file__))
vendor_src = os.path.join(current_dir, "vendor")
vendor_dest = os.path.join(os.path.dirname(current_dir), "vendor")

if os.path.exists(vendor_src):
    # 如果目标位置已存在 vendor 文件夹，合并内容
    if os.path.exists(vendor_dest):
        print("Merging vendor folders...")
        # 遍历源 vendor 文件夹中的所有内容
        for item in os.listdir(vendor_src):
            src_path = os.path.join(vendor_src, item)
            dest_path = os.path.join(vendor_dest, item)
            
            # 如果是文件，直接复制
            if os.path.isfile(src_path):
                shutil.copy2(src_path, dest_path)
                print(f"Copied file: {item}")
            # 如果是文件夹，递归合并
            elif os.path.isdir(src_path):
                if os.path.exists(dest_path):
                    # 如果目标文件夹已存在，递归合并子文件夹
                    for sub_item in os.listdir(src_path):
                        sub_src_path = os.path.join(src_path, sub_item)
                        sub_dest_path = os.path.join(dest_path, sub_item)
                        if os.path.isfile(sub_src_path):
                            shutil.copy2(sub_src_path, sub_dest_path)
                        elif os.path.isdir(sub_src_path):
                            shutil.copytree(sub_src_path, sub_dest_path, dirs_exist_ok=True)
                else:
                    # 如果目标文件夹不存在，直接复制整个文件夹
                    shutil.copytree(src_path, dest_path)
                print(f"Merged directory: {item}")
        
        # 删除源 vendor 文件夹
        shutil.rmtree(vendor_src)
        print("Merged vendor folders successfully.")
    else:
        # 如果目标位置不存在 vendor 文件夹，直接移动
        shutil.move(vendor_src, vendor_dest)
        print(f"Moved vendor folder from {vendor_src} to {vendor_dest}")
    
    # 更新 premake_exe 路径
    premake_dir = os.path.join(vendor_dest, "premake/bin")
    premake_exe = os.path.join(premake_dir, "premake5.exe")

print("Running premake...")
subprocess.call([premake_exe, "vs2022"])

