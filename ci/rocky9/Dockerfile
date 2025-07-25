# Digest from https://hub.docker.com/_/rockylinux/tags?name=9
FROM rockylinux:9@sha256:d7be1c094cc5845ee815d4632fe377514ee6ebcf8efaed6892889657e5ddaaa6

COPY requirements.txt ./requirements.txt

# Development tools
RUN dnf -y --setopt install_weak_deps=false groupinstall 'Development Tools' \
# for dnf config-manager plugin
&& dnf install -y --setopt install_weak_deps=false python3-dnf-plugins-core \
&& dnf install -y \
# Install epel-release rpm (Needed for libglade2-devel. Must be a separate step)
https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm \
&& dnf config-manager --enable crb \
# Centos build utilities
&& dnf -y --setopt install_weak_deps=false install \
libtool-ltdl-devel \
libedit-devel \
cmake \
# security-hardened alternatives of packages
rocky-release-security \
# Option glut
freeglut-devel \
# Option gts
# Option swig
swig \
# Command smyra
libglade2-devel \
# Command gvedit
qt5-devel \
# Plugin library devil
# Plugin library gd
gd-devel \
# Documentation
ghostscript \
# Plugin library
ghostscript-devel \
# Plugin library gdk
gtk3-devel \
# Plugin library lasi
# Plugin library pangocairo
# Plugin library poppler
poppler-glib-devel \
poppler-qt5 \
# Plugin library rsvg
librsvg2-devel \
# Plugin library webp
# Language extension gv_guile
guile30-devel \
# Language extension gv_java
java-devel \
# Language extension gv_lua
lua-devel \
# Language extension gv_perl
perl \
perl-ExtUtils-Embed \
# Language extension gv_php
php-devel \
openssl-devel \
# Language extension gv_python3
python3-devel \
# Language extension gv_ruby
ruby-devel \
# Language extension gv_tcl
tcl-devel \
libX11-devel \
libXrender-devel \
tk-devel \
expat-devel \
gmp-devel \
# create a Python virtual environment
&& python3 -m venv /opt/virtualenv \
# install Python dependencies within the virtual environment
&& env PATH="/opt/virtualenv/bin:$PATH" python3 -m pip install --requirement requirements.txt \
# Remove possible old version of Graphviz so that we can detect if
# individual Graphviz tools are missing in the current release
&& dnf remove -y graphviz \
# Clean up
&& dnf clean all && rm -rf /var/cache/dnf
