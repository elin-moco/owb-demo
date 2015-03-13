'use strict';

$(function () {

    var BLESHIELD_SERVICE_UUID = "713D0000-503E-4C75-BA94-3148F18D941E".toLowerCase();
    var BLESHIELD_TX_UUID = "713D0002-503E-4C75-BA94-3148F18D941E".toLowerCase();
    var BLESHIELD_RX_UUID = "713D0003-503E-4C75-BA94-3148F18D941E".toLowerCase();
    var CLIENT_CHARACTERISTIC_CONFIG_UUID = "00002902-0000-1000-8000-00805f9b34fb".toLowerCase();
    var service_id;
    var char_tx_id;
    var char_rx_id;
    var descr_ccc_id;

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
            conn_id = undefined;
            $('#connect_state').html('disconnected');
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
        if (event.srvc_id_id_uuid == BLESHIELD_SERVICE_UUID) {
            service_id = {
                uuid: event.srvc_id_id_uuid,
                inst_id: event.srvc_id_id_inst_id,
                is_primary: event.srvc_id_is_primary
            };

            bleManager.getCharacteristic(conn_id, service_id, start_char_id);
        }
        front_page = 0;
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
        if (start_char_id && start_char_id.uuid == char_id.uuid) {
            return;
        }
        if (event.char_id_uuid == BLESHIELD_RX_UUID) {
            char_rx_id = char_id;
            //Write to BLE Shield
//            bleManager.registerForNotification(client_if, bd_addr, service_id, char_rx_id);
        }
        if (event.char_id_uuid == BLESHIELD_TX_UUID) {
            char_tx_id = char_id;

            bleManager.registerForNotification(client_if, bd_addr, service_id, char_tx_id);
            bleManager.getDescriptor(conn_id, service_id, char_tx_id, start_descr_id);
        }
        start_char_id = char_id;
        bleManager.getCharacteristic(conn_id, service_id, char_id);
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
        if (start_descr_id && start_descr_id.uuid == descr_id.uuid) {
            return;
        }
        if (event.descr_id_uuid == CLIENT_CHARACTERISTIC_CONFIG_UUID) {
            descr_ccc_id = descr_id;
//            var descWritten = bleManager.writeDescriptor(conn_id, service_id, char_tx_id, descr_ccc_id, write_type, 4, auth_req, '0100');
//            console.info('writeDescriptor', descWritten);
        }
        start_descr_id = descr_id;
//        keepAlive();
    }

    function keepAlive() {
//        setInterval(function() {
//            bleManager.readCharacteristic(conn_id, service_id, char_tx_id, auth_req);
//            bleManager.readDescriptor(conn_id, service_id, char_tx_id, descr_ccc_id, auth_req);
//            bleManager.getDescriptor(conn_id, service_id, char_tx_id, start_descr_id);
//        }, 1000);
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
        var characteristic = event.value;
        var pin = parseInt(characteristic.substr(0, 2), 16);
        var content = parseInt(characteristic.substr(2, 4), 16);
        if (pin == 0x0A) {
            $('#digital').text((content == 0x0100) ? 'On' : 'Off');
        }
        else if (pin == 0x0B) {
            $('#analog').text(content);
        }
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
//        bleManager.executeWrite(conn_id, 1);
    }

    function onReadDescriptor(event) {
        console.info("onReadDescriptor:" + event.value);
        var value = event.value;
        $('#des_read_data').html(value);
    }

    function onWriteDescriptor(event) {
        console.info("onWriteDescriptor status:" + event.status);
//        bleManager.executeWrite(conn_id, 1);
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
                start_char_id = {
                    uuid: "",
                    inst_id: "",
                    is_primary: ""
                };
                break;

            default :
                break;
        }
        front_page --;
    });

    function showDeviceList(show) {
        if (show) {
            $('#list_content').show();
            $('#search').show();
            $('#back').hide();
            $('#search_service').hide();
            $('#device_content').hide();
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

    function showSearching(searching) {
        if (searching) {
            $("#search").html('searching');
            $("#search").attr('disabled',true);
        } else {
            $("#search").html('search');
            $("#search").attr('disabled',false);
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

                $('#connect_state').html('Connecting...');
                showDevice(true, device);
                bleManager.connectBle(client_if, device.address, true);
                bd_addr = device.address;
                console.info('bd_addr: ' + bd_addr);
            });
    }

    $('#reset-device').click(function() {
        var charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, '040000');
        console.info('writeCharacteristic: ', charWritten);
    });
    $('#notification').change(function() {
        //Enable notifications from BLE Shield
        var descWritten;
        if($(this).is(':checked')) {
            descWritten = bleManager.writeDescriptor(conn_id, service_id, char_tx_id, descr_ccc_id, write_type, 4, auth_req, '0100');
        }
        else {
            descWritten = bleManager.writeDescriptor(conn_id, service_id, char_tx_id, descr_ccc_id, write_type, 4, auth_req, '0000');
        }
        console.info('writeDescriptor', descWritten);
    });
    $('#digital-out').change(function() {
        var charWritten;
        if($(this).is(':checked')) {
            charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, '010100');
        }
        else {
            charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, '010000');
        }
        console.info('writeCharacteristic: ', charWritten);
    });
    $('#servo').change(function() {
        var angle = zeroFill(parseInt($(this).val()).toString(16),2);
        var charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, '03'+angle+'00');
        console.info('writeCharacteristic: ', charWritten);
    });
    $('#analog-in').change(function() {
        var charWritten;
        if($(this).is(':checked')) {
            charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, 'A00100');
        }
        else {
            charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, 'A00000');
        }
        console.info('writeCharacteristic: ', charWritten);
    });

    function zeroFill( number, width )
    {
      width -= number.toString().length;
      if ( width > 0 )
      {
        return new Array( width + (/\./.test( number ) ? 2 : 1) ).join( '0' ) + number;
      }
      return number + ""; // always return a string
    }

});
