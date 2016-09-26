#
# Copyright (C) 2009-2010 Jo-Philipp Wich <xm@subsignal.org>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=ble_mgr
PKG_RELEASE:=1
PKG_VERSION:=1.0
PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/ble_mgr
  SECTION:=tigercel
  CATEGORY:=tigercel
  TITLE:= ble interface to manage router
  DEPENDS:= +libpthread +libsyslog +librt
endef

define Package/ble_mgr/description
 This package help to manage router using ble.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	make -C $(PKG_BUILD_DIR)  \
                CC="$(TARGET_CC)"
endef

define Build/InstallDev
endef

define Package/ble_mgr/install
	$(INSTALL_DIR) $(1)/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ble_mgr $(1)/bin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/ble_mgr.init $(1)/etc/init.d/ble_mgr
endef

$(eval $(call BuildPackage,ble_mgr))
