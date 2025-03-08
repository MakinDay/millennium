import importlib.metadata
import json
import os
import platform
import subprocess
import threading
from core.api.plugins import find_all_plugins
from core.util.logger import logger
import platform
from config import Config

def get_installed_packages():
    logger.log("Installed packages:")
    
    package_map = {dist.metadata["Name"]: dist.version for dist in importlib.metadata.distributions()}
    
    for name, version in package_map.items():
        logger.log(f"  {name}=={version}")
    
    return package_map

def process_output_handler(proc, outfile, terminate_flag):
    with open(outfile, 'w') as f:
        for line in iter(proc.stdout.readline, b''):
            if terminate_flag[0]:
                break

            line = line.rstrip()
            if line:
                logger.log(line)
                f.write(line + '\n')


def pip(cmd, config: Config):

    python_bin = config.get_python_path()
    pip_logs = config.get_pip_logs()
    terminate_flag = [False]

    os.makedirs(os.path.dirname(pip_logs), exist_ok=True)

    with open(pip_logs, 'w') as f:
        proc = subprocess.Popen(
            [python_bin, '-m', 'pip'] + cmd + ["--no-warn-script-location"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            bufsize=1, universal_newlines=True,
            creationflags=subprocess.CREATE_NO_WINDOW if platform.system() == 'Windows' else 0
        )

        output_handler_thread = threading.Thread(target=process_output_handler, args=(proc, pip_logs, terminate_flag))
        output_handler_thread.start()

        proc.wait()
        terminate_flag[0] = True
        output_handler_thread.join()

        if proc.returncode != 0:
            logger.error(f"PIP failed with exit code {proc.returncode}")


def install_packages(package_names, config: Config):
    pip(["install"] + package_names, config)


def uninstall_packages(package_names, config: Config):
    pip(["uninstall", "-y"] + package_names, config)


_platform = platform.system()

def needed_packages():

    logger.log(f"checking for packages on {_platform}")

    needed_packages = []
    installed_packages = get_installed_packages()

    for plugin in json.loads(find_all_plugins(logger)):
        requirements_path = os.path.join(plugin["path"], "requirements.txt")

        if not os.path.exists(requirements_path):
            continue

        with open(requirements_path, "r") as f:
            for package in f.readlines():

                package_name = package.strip() 
                package_platform = _platform

                if "|" in package_name:
                    split = package_name.split(" | ")

                    package_name = split[0]
                    package_platform = split[1]

                # Maintain backwards compatibility with old format that doesn't contain a version
                if package_name.find("==") == -1:
                    if package_name not in installed_packages:
                        if package_platform == _platform:
                            logger.log(f"Package {package_name} is not installed, installing...")
                            needed_packages.append(package_name)
                            break        
                # New format, offer better version control support
                else:
                    if package_name.split("==")[0] not in installed_packages:

                        if package_platform == _platform:
                            logger.log(f"Package {package_name} is not installed, installing...")
                            needed_packages.append(package_name)
                            break
                
                    elif package_name.split("==")[0] in installed_packages:

                        if package_platform == _platform:
                            if package_name.split("==")[1] != installed_packages[package_name.split("==")[0]]:
                                logger.log(f"Package {package_name} is outdated. Current version: {installed_packages[package_name.split('==')[0]]}, required version: {package_name.split('==')[1]}")
                                needed_packages.append(package_name)
                                break

    return needed_packages


def audit(config: Config):
    packages = needed_packages()

    if packages:
        logger.log(f"Installing packages: {packages}")
        install_packages(packages, config)
    else:
        logger.log("All required packages are satisfied.")