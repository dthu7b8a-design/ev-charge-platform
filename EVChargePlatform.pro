TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = shared server user admin
server.depends = shared
user.depends = shared server
admin.depends = shared server
