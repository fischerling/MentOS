#!/bin/sh

ROOT=$1

if [ -z "${ROOT}" ]
then
	echo "Usage: $0 <root>"
	exit 1
fi

# Set default permissions and file owner
chown root:root -R "${ROOT}"
sudo chmod -R u=rwX,go=rX -R "${ROOT}"

# Set security related permissions
sudo chmod og-rwx "${ROOT}"/etc/passwd

# Set user permissions
sudo chown 1000:1000 -R "${ROOT}"/home/alice
sudo chown 1001:1001 -R "${ROOT}"/home/bob
sudo chmod -R u=rwX,go= -R "${ROOT}"/home/*
