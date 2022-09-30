# Necessary files for Elastic-Hurryup
sudo apt-get update
sudo apt-get install -y linux-tools-$(uname -r) #cpupower
#sudo apt-get install acpi-cpufreq #acpi

# Force acpi
cd /etc/default
sudo sed -i 's/GRUB_CMDLINE_LINUX="/GRUB_CMDLINE_LINUX="intel_pstate=nowhwp intel_pstate=disable acpi=force /g' grub

sudo update-grub
# sudo reboot
