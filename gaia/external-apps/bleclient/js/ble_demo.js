'use strict';

$(function () {

    const TAG = "BluetoothGatt";
    var REGISTER_UUID = "";
    var bleControl = BleCattClientControl.getInstance();
    var bluetoothManager = bleControl.getBluetooth();
    var bleManager = bleControl.getBleGatt();
    var settingManager = bleControl.getSetting();

    var searchTimer = undefined;
    var scaning = false;
    var defaultAdapter = null;
    var service_scaning = false;
    var rssi_timer = undefined;

    var client_if;
    var server_if;
    var bd_addr;

    var regist_uuid;
    var conn_id;

    var select_srvc_id;
    var select_char_id;
    var select_descr_id;

    var start_incl_srvc_id = {
        uuid: "",
        inst_id: ""
    };
    var start_char_id = {
        uuid: "",
        inst_id: "",
        is_primary: ""
    };
    var start_descr_id = {
        uuid: "",
        inst_id: ""
    };

    var auth_req = 0;
    var write_type = 1;

    var front_page = -1;

    window.onunload = function () {
        if (client_if) {
            if (scaning) {
                bleManager.scanLEDevice(client_if, false);
            }
            bleManager.unRegisterClient(client_if);
            client_if = undefined;
            defaultAdapter = undefined;
            bleManager = undefined;
            settingManager = undefined;
            bleControl = undefined;
        }
    };

    bluetoothManager.onenabled = registerCallback;

    bluetoothManager.ondisabled = function () {
        console.info("bluetooth disabled");
        defaultAdapter = null;
    };

    var req = settingManager.createLock().get('bluetooth.enabled');
    req.onsuccess = function() {
        var enabled = req.result['bluetooth.enabled'];
        console.info("bluetooth enabled:" + enabled);
        if (enabled) {
            registerCallback();
        } else {
            alert("Bluetooth will be opened");
            settingManager.createLock().set({
                'bluetooth.enabled': true
            });
        }
    };

    function registerCallback() {
        console.info("registerCallback");
        defaultAdapter = null;
        var req = bluetoothManager.getDefaultAdapter();
        req.onsuccess = function bt_getAdapterSuccess() {
            defaultAdapter = req.result;
            if (defaultAdapter != null) {
                console.info("defaultAdapter:" + defaultAdapter.name);
                defaultAdapter.onregisterclient = onRegisterClient;
                defaultAdapter.onscanresult = onScanResult;
                defaultAdapter.onconnectble = onConnectble;
                defaultAdapter.ondisconnectble = onDisconnectble;
                defaultAdapter.onsearchcomplete = onSearchComplete;
                defaultAdapter.onsearchresult = onSearchResult;
                defaultAdapter.ongetcharacteristic = onGetCharacteristic;
                defaultAdapter.ongetdescriptor = onGetDescriptor;
                defaultAdapter.ongetIncludedservice = onGetIncludedService;
                defaultAdapter.onregisterfornotification = onRegisterforNotification;
                defaultAdapter.onnotify = onNotify;
                defaultAdapter.onreadcharacteristic = onReadCharacteristic;
                defaultAdapter.onwritecharacteristic = onWriteCharacteristic;
                defaultAdapter.onreaddescriptor = onReadDescriptor;
                defaultAdapter.onwritedescriptor = onWriteDescriptor;
                defaultAdapter.onexecuteWrite = onExecutWrite;
                defaultAdapter.onreadremoterssi = onReadRemoterssi;
                defaultAdapter.onblelisten = onBleListen;

                if (bleManager) {
                    bleManager.registerClient(REGISTER_UUID);
                }
            } else {
                Log.w(TAG, 'bluetooth adapter is null');
            }
        };
        req.onerror = function bt_getAdapterFailed() {
            console.info('Can not get bluetooth adapter!');
        };
    }

    function onRegisterClient(event) {
        console.info("register status:" + event.status);
        console.info("register client_if:" + event.client_if);
        console.info("register uuid:" + event.uuid);
        if (event.status == 0) {
            regist_uuid = event.uuid;
            client_if = event.client_if;
            console.info('regist_uuid: ' + regist_uuid);
            console.info('client_if: ' + client_if);
            scanDevices();

        }
    }

    function onScanResult(event) {
        console.info("onScanResult:" + event.bda);
        var device = {
            name : event.adv_data,
            address : event.bda,
            rssi : event.rssi,
            type : event.device_type
        };
        addDevice(device);
    }

    function onConnectble(event) {
        console.info("connectble status:" + event.status);
        console.info("connectble conn_id:" + event.conn_id);
        if (event.status == 0) {
            $('#connect_state').html('SearchService...');
            conn_id = event.conn_id;
            $("#service_list li").remove();
            service_scaning = true;
            bleManager.searchService(conn_id, '');
            if (!rssi_timer) {
                rssi_timer = setInterval(function() {
                    bleManager.readRemoteRssi(client_if, bd_addr);
                }, 5000);
            }
        }
    }

    function onDisconnectble(event) {
        console.info("disconnectble status:" + event.status);
        if (event.status == 0) {
            clearInterval(rssi_timer);
            rssi_timer = undefined;
            front_page = 0;
            conn_id = undefined;
            $('#connect_state').html('disconnected');
            showCharacteristic(false);
            showCharacteristicList(false);
            showDescriptor(false);
            showDescriptorList(false);
            showServiceList(false);
        }
        $('#path').html('');
    }

    function onSearchComplete(event) {
        console.info("onSearchComplete status:" + event.status);
        service_scaning = false;
    }

    function onSearchResult(event) {
        $('#connect_state').html('Connected');
        console.info("onSearchResult:" + event);
        console.info("srvc_id_id_uuid:" + event.srvc_id_id_uuid);
        console.info("srvc_id_id_inst_id:" + event.srvc_id_id_inst_id);
        console.info("srvc_id_is_primary:" + event.srvc_id_is_primary);
        var srvc_id = {
            uuid: event.srvc_id_id_uuid,
            inst_id: event.srvc_id_id_inst_id,
            is_primary: event.srvc_id_is_primary
        };

        front_page = 1;
        addService(srvc_id);
        showServiceList(true);
    }

    function onGetCharacteristic(event) {
        console.info("onGetCharacteristic:" + event);
        console.info("state:" + event.status);
        console.info("char_id_uuid:" + event.char_id_uuid);
        console.info("char_id_inst_id:" + event.char_id_inst_id);
        console.info("char_prop:" + event.char_prop);

        var char_id = {
            uuid: event.char_id_uuid,
            inst_id: event.char_id_inst_id
        };

        var characteristic = {
            uuid: event.char_id_uuid,
            inst_id: event.char_id_inst_id,
            prop: event.char_prop
        };
        addCharacteristic(characteristic, char_id);
        showCharacteristicList(true);
    }

    function onGetDescriptor(event) {
        console.info("descr_status:" + event.status);
        console.info("descr_id_uuid:" + event.descr_id_uuid);
        console.info("descr_id_inst_id:"  + event.descr_id_inst_id);

        if (event.status != 0) {
            return;
        }
        var descr_id = {
            uuid: event.descr_id_uuid,
            inst_id: event.descr_id_inst_id
        };
        addDescriptor(descr_id, descr_id);
    }

    function onGetIncludedService(event) {
        console.info("onGetIncludedService:" + event);
        console.info("incl_srvc_id_id_uuid:" + event.incl_srvc_id_id_uuid);
        console.info("incl_srvc_id_id_inst_id:" + event.incl_srvc_id_id_inst_id);
        console.info("incl_srvc_id_is_primary:" + event.incl_srvc_id_is_primary);
    }

    function onRegisterforNotification(event) {
        console.info("onRegisterforNotification srvc_id_id_uuid:" + event.srvc_id_id_uuid);
        console.info("onRegisterforNotification char_id_uuid:" + event.char_id_uuid);
        console.info("onRegisterforNotification registered:" + event.registered);
        console.info("onRegisterforNotification status:" + event.status);
    }

    function onNotify(event) {
        console.info("onNotify value:" + event.value);
        console.info("onNotify bda:" + event.bda);
        console.info("onNotify srvc_id_id_uuid:" + event.srvc_id_id_uuid);
        console.info("onNotify srvc_id_id_inst_id:" + event.srvc_id_id_inst_id);
        console.info("onNotify srvc_id_is_primary:" + event.srvc_id_is_primary);
        console.info("onNotify char_id_uuid:" + event.char_id_uuid);
        console.info("onNotify char_id_inst_id:" + event.char_id_inst_id);
        console.info("onNotify len:" + event.len);
        console.info("onNotify is_notify:" + event.is_notify);
    }

    function onReadCharacteristic(event) {
        console.info("onReadCharacteristic srvc_id_id_uuid:" + event.srvc_id_id_uuid);
        console.info("onReadCharacteristic char_id_uuid:" + event.char_id_uuid);
        console.info("onReadCharacteristic descr_id_inst_id:" + event.descr_id_inst_id);
        console.info("onReadCharacteristic status:" + event.status);
        console.info("onReadCharacteristic value:" + event.value);
        console.info("value_type:" + event.value_type);
        var value = event.value;
        $('#char_read_data').html(value);

    }

    function onWriteCharacteristic(event) {
        console.info("onWriteCharacteristic status:" + event.status);
        bleManager.executeWrite(conn_id, 0);
    }

    function onReadDescriptor(event) {
        console.info("onReadDescriptor:" + event.value);
        var value = event.value;
        $('#des_read_data').html(value);
    }

    function onWriteDescriptor(event) {
        console.info("onWriteDescriptor status:" + event.status);
        bleManager.executeWrite(conn_id, 0);
    }

    function onExecutWrite(event) {
        console.info("onExecutWrite status:" + event.status);
    }

    function onReadRemoterssi(event) {
        $('#device_rssi').html(event.rssi);
    }

    function onBleListen(event) {
        console.info("onBleListen status:" + event.status);
        console.info("onBleListen server_if:" + event.server_if);
        server_if = event.server_if;
    }

    function scanDevices() {
        if (scaning) {
            return;
        }

        showSearching(true);
        $("#device_list li").remove();
        scaning = true;
        bleManager.scanLEDevice(client_if, true);
        searchTimer = setTimeout(function () {
            bleManager.scanLEDevice(client_if, false);
            clearTimeout(searchTimer);
            searchTimer = undefined;
            scaning = false;
            showSearching(false);
        }, 10000);
    }

    $("#search").on("click", function () {
        if (!defaultAdapter) {
            alert("Bluetooth should be opened");
            return;
        }
        scanDevices();
    });

    $('#back').on('click', function () {
        console.info("click back:" + front_page);
        if (front_page < 0) {
            return;
        }

        switch (front_page) {
            case 0:
                console.info("disconnect conn_id:" + conn_id);
                if (conn_id) {
                    bleManager.disconnectBle(client_if, bd_addr, conn_id);
                }
                showDeviceList(true);
                break;

            case 1:
                showServiceList(false);
                break;

            case 2:
                showCharacteristicList(false);
                $("#service_list li").remove();
                showServiceList(true);
                showPath(null, null, null, true);
                $('#connect_state').html('SearchService...');
                service_scaning = true;
                bleManager.searchService(conn_id, '');
                break;

            case 3:
                showDescriptorList(false);
                $("#descriptor_list li").remove();
                showCharacteristic(false);
                $("#characteristic_list li").remove();
                showCharacteristicList(true);
                showPath(null, null, null, true);
                start_char_id = {
                    uuid: "",
                    inst_id: "",
                    is_primary: ""
                };
                bleManager.getCharacteristic(conn_id, select_srvc_id, start_char_id);
                break;

            case 4:
                showDescriptor(false);
                $("#descriptor_list li").remove();
                showCharacteristic(true);
                showDescriptorList(true);
                showPath(null, null, null, true);
                start_descr_id = {
                    uuid: "",
                    inst_id: ""
                };
                bleManager.getDescriptor(conn_id, select_srvc_id, select_char_id, start_descr_id);
                break;

            default :
                break;
        }
        front_page --;
    });

    $('#search_service').on('click', function() {
        console.info("click service search");
        if (!defaultAdapter) {
            alert("Bluetooth should be opened");
            return;
        }
        if (!conn_id) {
            alert("The device disconnected");
            return;
        }
        if (service_scaning) {
            return;
        }
        showDescriptor(false);
        showDescriptorList(false);
        showCharacteristic(false);
        showCharacteristicList(false);
        $('#path').html('');
        $('#service_list li').remove();
        $('#connect_state').html('SearchService...');
        service_scaning = true;
        bleManager.searchService(conn_id, '');
    });

    $('#char_read').on('click', function() {
        console.info("click read characteristic:" + select_char_id.uuid);
        if (select_char_id) {
            bleManager.readCharacteristic(conn_id, select_srvc_id, select_char_id, auth_req);
        } else {
            Log.e(TAG, "char_id is null");
        }
    });

    $('#char_write').on('click', function() {
        console.info("click write characteristic");
        var cancel = {
            title: 'Cancel',
            cancel: function() {
                console.info("cancel");
                Dialog.hidden();
            }
        };
        var confirm = {
            title: 'OK',
            ok: function (value) {
                console.info("confirm ok:" + value);
                if (value) {
                    var len = value.length;
                    console.info(value + ":" + len);
                    var charWritten = bleManager.writeCharacteristic(conn_id, select_srvc_id, select_char_id, write_type, len, auth_req, value);
                    console.info('writeCharacteristic: ', charWritten);
                }
                Dialog.hidden();
            }
        };
        Dialog.show("Input the characteristic:", null, cancel, confirm);
    });

    $('#des_read').on('click', function() {
        console.info("click read descriptor:" + select_descr_id.uuid);
        if (select_descr_id) {
            bleManager.readDescriptor(conn_id, select_srvc_id, select_char_id, select_descr_id, auth_req);
        } else {
            Log.e(TAG, "descr_id is null");
        }
    });

    $('#des_write').on('click', function() {
        console.info("click write descriptor:");
        var cancel = {
            title: 'Cancel',
            cancel: function() {
                console.info("cancel");
                Dialog.hidden();
            }
        };
        var confirm = {
            title: 'OK',
            ok: function (value) {
                console.info("confirm ok:" + value);
                if (value) {
                    var len = value.length;
                    console.info(value + ":" + len);
                    var descWritten = bleManager.writeDescriptor(conn_id, select_srvc_id, select_char_id, select_descr_id, write_type, len, auth_req, value);
                    console.info('writeDescriptor', descWritten);
                }
                Dialog.hidden();
            }
        };
        Dialog.show("Input the descriptor:", null, cancel, confirm);
    });

    function showDeviceList(show) {
        if (show) {
            $('#list_content').show();
            $('#search').show();
            $('#back').hide();
            $('#search_service').hide();
            $('#device_content').hide();
            showCharacteristicList(false);
            showCharacteristic(false);
            showDescriptorList(false);
            showDescriptor(false);
            showServiceList(false);
        }
    }

    function showDevice(show, device) {
        if (show) {
            if (device) {
                $('#device_name').html(device.name);
                $('#device_address').html(device.address);
                $('#device_rssi').html(device.rssi);
                $('#device_type').html(device.type);
            }

            $('#list_content').hide();
            $('#search').hide();
            $('#back').show();
            $('#search_service').show();
            $('#device_content').show();
        }
    }

    function showServiceList(show) {
        if (show) {
            $('#service_list_div').show();
        } else {
            $('#service_list_div').hide();
        }
    }

    function showCharacteristicList(show) {
        if (show) {
            $('#characteristic_list_div').show();
        } else {
            $('#characteristic_list_div').hide();
        }
    }

    function showCharacteristic(show) {
        $('#char_read_data').html('');
        if (show) {
            $('#characteristic_details').show();
        } else {
            $('#characteristic_details').hide();
        }
    }

    function showDescriptorList(show) {
        console.info("show descrpt:" + show);
        if (show) {
            $('#descriptor_list_div').show();
        } else {
            $('#descriptor_list_div').hide();
        }
    }

    function showDescriptor(show) {
        $('#des_read_data').html('');
        if (show) {
            $('#descriptor_details').show();
        } else {
            $('#descriptor_details').hide();
        }
    }

    function showSearching(searching) {
        if (searching) {
            $("#search").html('searching');
            $("#search").attr('disabled',true);
        } else {
            $("#search").html('search');
            $("#search").attr('disabled',false);
        }
    }

    function showPath(service, characteristic, descripter, back) {
        var path = $('#path');
        if (service) {
            path.html("Service:" + service);
        }
        if (characteristic) {
            path.html(path.html() + "/  Characteristic:" + characteristic);
        }
        if (descripter) {
            path.html(path.html() + "/  Descripter:" + descripter);
        }
        if (back) {
            var values = path.html();
            if (values) {
                path.html('');
                if (values.contains('/')) {
                    var arr = new Array();
                    arr = values.split('/');
                    arr.pop();
                    arr.forEach(function(value) {
                        if (path.html()) {
                            path.html(path.html() + "/  " + value);
                        } else {
                            path.html(value);
                        }
                    });
                }
            }
        }
    }

    function addDevice(device) {
        var item = $("<li><a href='#'>" + device.name + "</a></li>");
        $("#device_list").append(item).find("li:last").hide();
        $('ul').listview('refresh');
        $("#device_list").find("li:last").slideDown(300)
            .click(function () {
                if (scaning) {
                    bleManager.scanLEDevice(client_if, false);
                    clearTimeout(searchTimer);
                    searchTimer = undefined;
                    scaning = false;
                    showSearching(false);
                }

                if (!defaultAdapter) {
                    alert("Bluetooth should be opened");
                    return;
                }

                front_page = 0;
                $('#connect_state').html('Connecting...');
                showDevice(true, device);
                bleManager.connectBle(client_if, device.address, true);
                bd_addr = device.address;
                console.info('bd_addr: ' + bd_addr);
            });
    }

    function addService(service) {
        var uuid = service.uuid | 'uuid';
        var instance_id = service.inst_id | '0';
        var type = service.primary | '1';
        var item = $("<li><a href='#'>" + service.uuid + "</a></li>");
        $("#service_list").append(item).find("li:last").hide();
        $('ul').listview('refresh');
        $("#service_list").find("li:last").slideDown(300)
            .click(function () {
                front_page = 2;
                showServiceList(false);
                showPath(service.uuid, null, null);
                $('#characteristic_list li').remove();
                showCharacteristicList(true);
                select_srvc_id = service;
                bleManager.getIncludeService(conn_id, select_srvc_id, start_incl_srvc_id);
                start_char_id = {
                    uuid: "",
                    inst_id: "",
                    is_primary: ""
                };
                bleManager.getCharacteristic(conn_id, select_srvc_id, start_char_id);
            });
    }

    function addCharacteristic(characteristic, char_id) {
        if (start_char_id && start_char_id.uuid == char_id.uuid) {
            return;
        }
        var uuid = characteristic.uuid | 'uuid';
        var instance_id = characteristic.inst_id | '0';
        var prop = characteristic.prop | 2;
        var item = $("<li><a href='#'>" + characteristic.uuid + "</a></li>");
        $("#characteristic_list").append(item).find("li:last").hide();
        $('ul').listview('refresh');
        $("#characteristic_list").find("li:last").slideDown(300)
            .click(function () {
                switch (prop){
                    case 8:
                        $("#char_read").hide();
                        $("#char_write").show();
                        $("#des_read").hide();
                        $("#des_write").show();
                        break;
                    case 10:
                        $("#char_read").show();
                        $("#char_write").show();
                        $("#des_read").show();
                        $("#des_write").show();
                        break;
                    default:
                        $("#char_read").show();
                        $("#char_write").show();
                        $("#des_read").show();
                        $("#des_write").show();
                        break;
                }

                front_page = 3;
                showCharacteristicList(false);
                showCharacteristic(true);
                showDescriptorList(true);
                showPath(null, characteristic.uuid, null);

                select_char_id = char_id;
                start_descr_id = {
                    uuid: "",
                    inst_id: ""
                };
                bleManager.getDescriptor(conn_id, select_srvc_id, select_char_id, start_descr_id);
                console.info('registerForNotification', client_if, bd_addr, select_srvc_id, select_char_id);
                bleManager.registerForNotification(client_if, bd_addr, select_srvc_id, select_char_id);
                bleManager.readCharacteristic(conn_id, select_srvc_id, select_char_id, auth_req);
            });
        start_char_id = char_id;
        bleManager.getCharacteristic(conn_id, select_srvc_id, char_id);
    }

    function addDescriptor(descriptor, descr_id) {
        if (start_descr_id && start_descr_id.uuid == descr_id.uuid) {
            return;
        }
        var uuid = descriptor.uuid | 'uuid';
        var instance_id = descriptor.inst_id | '0';

        var item = $("<li><a href='#'>" + descriptor.uuid + "</a></li>");
        $("#descriptor_list").append(item).find("li:last").hide();
        $('ul').listview('refresh');
        $("#descriptor_list").find("li:last").slideDown(300)
            .click(function () {
                front_page = 4;
                showCharacteristic(false);
                showDescriptorList(false);
                showDescriptor(true);
                showPath(null, null, descriptor.uuid);

                select_descr_id = descr_id;
                bleManager.readDescriptor(conn_id, select_srvc_id, select_char_id, select_descr_id, auth_req);
            });
        start_descr_id = descr_id;
        bleManager.getDescriptor(conn_id, select_srvc_id, select_char_id, start_descr_id);
    }
});
