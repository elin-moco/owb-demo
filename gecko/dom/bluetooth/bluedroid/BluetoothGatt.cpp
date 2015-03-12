#include "BluetoothGatt.h"

#include "base/basictypes.h"

#include "DOMRequest.h"
#include "nsContentUtils.h"

#include "BluetoothCommon.h"
#include "BluetoothService.h"
#include "BluetoothSocket.h"
#include "BluetoothUtils.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "MainThreadUtils.h"
#include "nsIObserverService.h"
#include "nsThreadUtils.h"
#include "nsIObserver.h"

#define __DEBUG__

#define LOG_TAG "BluetoothGatt"

#ifdef __DEBUG__
#define DN_LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define DN_LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define DN_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define DN_LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define DN_LOGV(...)
#define DN_LOGW(...)
#define DN_LOGE(...)
#define DN_LOGI(...)
#endif

#define LOGV DN_LOGV
#define LOGW DN_LOGW
#define LOGE DN_LOGE
#define LOGI DN_LOGI

/**
 * Dispatch task with arguments to main thread.
 */
#define BT_HF_DISPATCH_MAIN(args...) \
  NS_DispatchToMainThread(new MainThreadTask(args))

/**
 * Process bluedroid callbacks with corresponding handlers.
 */
#define BT_GATT_PROCESS_CB(func, args...)          \
  do {                                           \
    NS_ENSURE_TRUE_VOID(sBluetoothGatt);   \
    sBluetoothGatt->func(args);            \
  } while(0)

#define STREAM_TO_UINT8(u8, p)   {u8 = (uint8_t)(*(p)); (p) += 1;}
#define HCI_EXT_INQ_RESPONSE_LEN        240
#define BT_EIR_SHORTENED_LOCAL_NAME_TYPE    0x08
#define BT_EIR_COMPLETE_LOCAL_NAME_TYPE     0x09
#define BT_EIR_MANUFACTURER_SPECIFIC_TYPE   0xFF

#define MAX_LEN_UUID_STR 37
#define MAX_HEX_VAL_STR_LEN 100
#define MAX_HEX_DESCRIPTOR_VAL_STR_LEN 200

using namespace mozilla;
USING_BLUETOOTH_NAMESPACE

static uint8_t char2int(char input)
{
  if(input >= '0' && input <= '9')
    return input - '0';
  if(input >= 'A' && input <= 'F')
    return input - 'A' + 10;
  if(input >= 'a' && input <= 'f')
    return input - 'a' + 10;
  return 0;
}

// This function assumes src to be a zero terminated sanitized string with
// an even number of [0-9a-f] characters, and target to be sufficiently large
static void hex2bin(const char* src, uint8_t* tar)
{
  memset(tar, 0, sizeof(tar));
  while(*src && src[1])
  {
    *(tar++) = char2int(*src)*16 + char2int(src[1]);
    src += 2;
  }
}

static inline void ntoh128(const bt_uuid_t *src, bt_uuid_t *dst)
{
    int i;

    for (i = 0; i < 16; i++)
        dst->uu[15 - i] = src->uu[i];
}

void
BtUuidToString(bt_uuid_t* uuid, nsAString& btUuid)
{
    char bdstr[48];

    unsigned int   data0;
    unsigned short data1;
    unsigned short data2;
    unsigned short data3;
    unsigned int   data4;
    unsigned short data5;

    bt_uuid_t nvalue;
    const uint8_t *data = (uint8_t *) &nvalue;

    ntoh128(uuid, &nvalue);

    memcpy(&data0, &data[0], 4);
    memcpy(&data1, &data[4], 2);
    memcpy(&data2, &data[6], 2);
    memcpy(&data3, &data[8], 2);
    memcpy(&data4, &data[10], 4);
    memcpy(&data5, &data[14], 2);

    snprintf(bdstr, MAX_LEN_UUID_STR, "%.8x-%.4x-%.4x-%.4x-%.8x%.4x",
            ntohl(data0), ntohs(data1),
            ntohs(data2), ntohs(data3),
            ntohl(data4), ntohs(data5));

      btUuid = NS_ConvertUTF8toUTF16(bdstr);
}

void
StringToUuid(nsAString& strUuid,
        bt_uuid_t *uuid)
{
    NS_ConvertUTF16toUTF8 uuidUTF8(strUuid);
    const char* str = uuidUTF8.get();

    uint32_t data0, data4;
    uint16_t data1, data2, data3, data5;
    bt_uuid_t n128, u128;
    uint8_t *val = (uint8_t *) &n128;

    if (sscanf(str, "%08x-%04hx-%04hx-%04hx-%08x%04hx",
                &data0, &data1, &data2,
                &data3, &data4, &data5) != 6)
    {
        return;
    }

    data0 = htonl(data0);
    data1 = htons(data1);
    data2 = htons(data2);
    data3 = htons(data3);
    data4 = htonl(data4);
    data5 = htons(data5);

    memcpy(&val[0], &data0, 4);
    memcpy(&val[4], &data1, 2);
    memcpy(&val[6], &data2, 2);
    memcpy(&val[8], &data3, 2);
    memcpy(&val[10], &data4, 4);
    memcpy(&val[14], &data5, 2);

    ntoh128(&n128, uuid);
}


uint8_t *CheckBeaconData( uint8_t *p_eir, uint8_t type, uint8_t *p_length, char *str )
{
    uint8_t *p = p_eir;
    p += 5;
    uint8_t cid0;
    uint8_t cid1;
    uint8_t typ;
    uint8_t len;
    STREAM_TO_UINT8(cid0, p);
    STREAM_TO_UINT8(cid1, p);
    STREAM_TO_UINT8(typ, p);
    STREAM_TO_UINT8(len, p);
    LOGI("manufacturer_data: %02X %02X %02X %02X", cid0, cid1, typ, len);
    LOGI("beacon_uuid: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", 
        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    if (0x4C == cid0 && 0x00 == cid1 && 0x02 == typ && 0x15 == len) {
        sprintf(str, "iBeacon (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", 
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        *p_length = 46;
        return (uint8_t *)str;
    }
    return NULL;
}
/*******************************************************************************
**
** Function         BTM_CheckEirData
**
** Description      This function is called to get EIR data from significant part.
**
** Parameters       p_eir - pointer of EIR significant part
**                  type   - finding EIR data type
**                  p_length - return the length of EIR data not including type
**
** Returns          pointer of EIR data
**
*******************************************************************************/
uint8_t *CheckEirData( uint8_t *p_eir, uint8_t type, uint8_t *p_length )
{
    uint8_t *p = p_eir;
    uint8_t length;
    uint8_t eir_type;
    LOGI("CheckEirData type=0x%02X", type);

    STREAM_TO_UINT8(length, p);
    LOGI("CheckEirData length=%d", length);
    while( length && (p - p_eir <= HCI_EXT_INQ_RESPONSE_LEN))
    {
        STREAM_TO_UINT8(eir_type, p);
        LOGI("CheckEirData eir_type=0x%02X", eir_type);
        if( eir_type == type )
        {
            /* length doesn't include itself */
            *p_length = length - 1; /* minus the length of type */
            return p;
        }
        p += length - 1; /* skip the length of data */
        STREAM_TO_UINT8(length, p);
        LOGI("CheckEirData length=%d", length);
    }

    *p_length = 0;
    return NULL;
}

/* Converts array of uint8_t to string representation */
static char *array2str(const uint8_t *v, int size, char *buf, int out_size)
{
    int limit = size;
    int i;

    if (out_size > 0) {
        *buf = '\0';
        if (size >= 2 * out_size)
            limit = (out_size - 2) / 2;

        for (i = 0; i < limit; ++i)
            sprintf(buf + 2 * i, "%02x", v[i]);

        /* output buffer not enough to hold whole field fill with ...*/
        if (limit < size)
            sprintf(buf + 2 * i, "...");
    }

    return buf;
}

/*
 * Tries to convert hex string of given size into out buffer.
 * Output buffer is little endian.
 */
static void scan_field(const char *str, int len, uint8_t *out, int out_size)
{
    int i;
    memset(out, 0, out_size);
    if (out_size * 2 > len + 1)
        out_size = (len + 1) / 2;
    for (i = 0; i < out_size && len > 0; ++i) {
        len -= 2;
        if (len >= 0)
            sscanf(str + len, "%02hhx", &out[out_size - i - 1]);
        else
            sscanf(str, "%1hhx", &out[out_size - i - 1]);
    }
}

namespace {
//StaticRefPtr<BluetoothGatt> sBluetoothGatt;
static BluetoothGatt* sBluetoothGatt;
static const btgatt_interface_t* sBluetoothGattInterface;
static nsString mTest;
}

// Main thread task commands
enum MainThreadTaskCmd {
  NOTIFY_GATT_CALLBACKS,
};

/** Callback invoked in response to register_client */
static void
GattRegisterClientCallback(int status, int client_if, bt_uuid_t *app_uuid)
{
    LOGW("GattRegisterClientCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessRegisterClient(status, client_if, app_uuid);
    }
    LOGI("callback--RegisterClient status:%d, client_if:%d, uuid : %s", status, client_if, "333333333333");
}

/** Callback for scan results */
static void
GattScanResultCallback(bt_bdaddr_t* bda, int rssi, uint8_t* adv_data)
{
    LOGW("GattScanResultCallback temp line:%d", __LINE__);

    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessScanLEDevice(bda, rssi, adv_data);
    }
}

/** GATT open callback invoked in response to open */
static void
GattConnectCallback(int conn_id, int status, int client_if, bt_bdaddr_t* bda)
{
    LOGW("GattConnectCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessConnectBle(conn_id, status, client_if, bda);
    }
}

/** Callback invoked in response to close */
static void
GattDisconnectCallback(int conn_id, int status,
        int client_if, bt_bdaddr_t* bda)
{
    LOGW("GattDisconnectCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessDisconnectBle(conn_id, status, client_if, bda);
    }
}

/**
 * Invoked in response to search_service when the GATT service search
 * has been completed.sBluetoothGatt
 */
static void
GattSearchCompleteCallback(int conn_id, int status)
{
    LOGW("GattSearchCompleteCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessSearchComplete(conn_id, status);
    }
}

/** Reports GATT services on a remote device */
static void
GattSearchResultCallback( int conn_id, btgatt_srvc_id_t *srvc_id)
{
    LOGW("GattSearchResultCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessSearchResult(conn_id, srvc_id);
    }
}

/** GATT characteristic enumeration result callback */
static void
GattGetCharacteristicCallback(int conn_id, int status,
        btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id, int char_prop)
{
    LOGW("GattGetCharacteristicCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessGetCharacteristic(conn_id, status, srvc_id, char_id, char_prop);
    }
}

/** GATT descriptor enumeration result callback */
static void
GattGetDescriptorCallback(int conn_id, int status,
        btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id,
        btgatt_gatt_id_t *descr_id)
{
    LOGW("GattGetDescriptorCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessGetDescriptor(conn_id, status, srvc_id, char_id, descr_id);
    }
}

/** GATT included service enumeration result callback */
static void
GattGetIncluded_serviceCallback(int conn_id, int status,
        btgatt_srvc_id_t *srvc_id, btgatt_srvc_id_t *incl_srvc_id)
{
    LOGW("GattGetIncluded_serviceCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessGetIncludeService(conn_id, status, srvc_id, incl_srvc_id);
    }
}

/** Callback invoked in response to [de]register_for_notification */
static void
GattRegisterForNotificationCallback(int conn_id,
        int registered, int status, btgatt_srvc_id_t *srvc_id,
        btgatt_gatt_id_t *char_id)
{
    LOGW("GattRegisterForNotificationCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessRegisterForNotification(conn_id, registered, status, srvc_id, char_id);
    }
}

/**
 * Remote device notification callback, invoked when a remote device sends
 * a notification or indication that a client has registered for.
 */
static void
GattNotifyCallback(int conn_id, btgatt_notify_params_t *p_data)
{
    LOGW("GattNotifyCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessNotify(conn_id, p_data);
    }
}

/** Reports result of a GATT read operation */
static void
GattReadCharacteristicCallback(int conn_id, int status,
        btgatt_read_params_t *p_data)
{
    LOGW("GattReadCharacteristicCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessReadCharacteristic(conn_id, status, p_data);
    }
}

/** GATT write characteristic operation callback */
static void
GattWriteCharacteristicCallback(int conn_id, int status,
        btgatt_write_params_t *p_data)
{
    LOGW("GattWriteCharacteristicCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessWriteCharacteristic(conn_id, status, p_data);
    }
}

/** GATT execute prepared write callback */
static void
GattExecuteWriteCallback(int conn_id, int status)
{
    LOGW("GattExecuteWriteCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessExecuteWrite(conn_id, status);
    }
}

/** Callback invoked in response to read_descriptor */
static void
GattReadDescriptorCallback(int conn_id, int status,
        btgatt_read_params_t *p_data)
{
    LOGW("GattReadDescriptorCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessReadDescriptor(conn_id, status, p_data);
    }
}

/** Callback invoked in response to write_descriptor */
static void
GattWriteDescriptorCallback(int conn_id, int status,
        btgatt_write_params_t *p_data)
{
    LOGW("GattWriteDescriptorCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessWriteDescriptor(conn_id, status, p_data);
    }
}

/** Callback triggered in response to read_remote_rssi */
static void
GattReadRemoteRssiCallback(int client_if, bt_bdaddr_t* bda,
        int rssi, int status)
{
    LOGW("GattReadRemoteRssiCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessReadRemoteRssi(client_if, bda, rssi, status);
    }
}

/**
 * Callback indicationg the status of a listen() operation
 */
static void
GattListenCallback(int status, int server_if)
{
    LOGW("GattListenCallback temp line:%d", __LINE__);
    if(sBluetoothGatt)
    {
        sBluetoothGatt->ProcessListen(status, server_if);
    }
}

static btgatt_callbacks_t sBtGattCallbacks;
static btgatt_client_callbacks_t sBtGattClientCallbacks = {
  GattRegisterClientCallback,
  GattScanResultCallback,
  GattConnectCallback,
  GattDisconnectCallback,
  GattSearchCompleteCallback,
  GattSearchResultCallback,
  GattGetCharacteristicCallback,
  GattGetDescriptorCallback,
  GattGetIncluded_serviceCallback,
  GattRegisterForNotificationCallback,
  GattNotifyCallback,
  GattReadCharacteristicCallback,
  GattWriteCharacteristicCallback,
  GattReadDescriptorCallback,
  GattWriteDescriptorCallback,
  GattExecuteWriteCallback,
  GattReadRemoteRssiCallback,
  GattListenCallback
};

class BluetoothGatt::MainThreadTask : public nsRunnable
{
public:
  MainThreadTask(const int aCommand,
                 const nsAString& aParameter = EmptyString())
    : mCommand(aCommand), mParameter(aParameter)
  {
  }

  nsresult Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(sBluetoothGatt);

    switch (mCommand) {
      case MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS:
          sBluetoothGatt->NotifyGattCallback(mParameter);
        break;
      default:
        LOGW("MainThreadTask: Unknown command %d", mCommand);
        break;
    }

    return NS_OK;
  }

private:
  int mCommand;
  nsString mParameter;
};

// static
void
BluetoothGatt::InitGattInterface()
{
    const bt_interface_t* btInf = GetBluetoothInterface();
    if(!btInf)
    {
        LOGE("GetBluetoothInterface is null");
        return;
    }

    sBluetoothGattInterface = (btgatt_interface_t *) btInf->get_profile_interface(BT_PROFILE_GATT_ID);
    if(!sBluetoothGattInterface)
    {
        LOGE("sBluetoothGattInterface is null");
        return;
    }

    /**register callbacks***/
    sBtGattCallbacks.client = &sBtGattClientCallbacks;
    if(BT_STATUS_SUCCESS != sBluetoothGattInterface->init(&sBtGattCallbacks)) //register gatt callback
    {
        LOGE("BluetoothGatt register gatt callbacks function failed");
    }
}

void
BluetoothGatt::DeInitGattInterface()
{
    if (sBluetoothGattInterface) {
        sBluetoothGattInterface->cleanup();
        sBluetoothGattInterface = nullptr;
    }
}

BluetoothGatt::BluetoothGatt()
{
    LOGW("start BluetoothGatt construct");
    LOGW("end BluetoothGatt construct");
}

// static
BluetoothGatt*
BluetoothGatt::Get()
{
    LOGI("start get");
    MOZ_ASSERT(NS_IsMainThread());

    // If sBluetoothgatt already exists, exit early
    if (sBluetoothGatt) {
        LOGI("sBluetoothGatt is not null");
        return sBluetoothGatt;
    }

    // If we're in shutdown, don't create a new instance
    //  NS_ENSURE_FALSE(sInShutdown, nullptr);

    // Create a new instance, register, and return
    BluetoothGatt* bluetoothGatt = new BluetoothGatt();
    sBluetoothGatt = bluetoothGatt;

    LOGI("end get");
    return sBluetoothGatt;
}

nsString
BluetoothGatt::getTest()
{
    return mTest;
}

BluetoothGatt::~BluetoothGatt()
{
    sBluetoothGattInterface->cleanup();
}

void
BluetoothGatt::NotifyGattCallback(const nsAString& aType)
{
    MOZ_ASSERT(NS_IsMainThread());

    // Dispatch an event of status change
    bool status;
    nsAutoString eventName;
    if (aType.EqualsLiteral(BLEGATT_REGISTER_CLIENT_ID)) {
        SendRegisterClientCallback();
    } else if(aType.EqualsLiteral(BLEGATT_SCAN_RESULT_ID))
    {
        SendScanLEDeviceCallback();
    } else if(aType.EqualsLiteral(BLEGATT_CONNECT_BLE_ID))
    {
        SendConnectBleCallback();
    } else if(aType.EqualsLiteral(BLEGATT_DISCONNECT_BLE_ID))
    {
        SendDisconnectBleCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_BLE_LISTEN_ID))
    {
        SendListenCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_SEARCH_COMPLETE_ID))
    {
        SendSearchCompleteCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_GET_CHRACTERISTIC_ID))
    {
        SendGetCharacteristicCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_GET_DESCRIPTOR_ID))
    {
        SendGetDescriptorCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_GET_INCLUDED_SERVICE_ID))
    {
        SendGetIncludeServiceCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_REGISTER_FOR_NOTIFICATION_ID))
    {
        SendRegisterForNotificationCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_NOTIFY_ID))
    {
        SendNotifyCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_READ_CHARACTERISTIC_ID))
    {
        SendReadCharacteristicCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_WRITER_HARACTERISTIC_ID))
    {
        SendWriteCharacteristicCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_READ_DESCRIPTOR_ID))
    {
        SendReadDescriptorCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_WRITE_DESCRIPTOR_ID))
    {
        SendWriteDescriptorCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_EXECUTE_WRITE_ID))
    {
        SendExecuteWriteCallback();
    }
    else if(aType.EqualsLiteral(BLEGATT_READ_REMOTERSSI_ID))
    {
        SendReadRemoteRssiCallback();
    }
    else {
        MOZ_ASSERT(false);
        return;
    }

}

bool BluetoothGatt::BluetoothGattOperate(uint32_t gattFunType, const nsTArray<nsString>& bleGattPara)
{
    LOGI("BluetoothGatt BluetoothGattOperate, gattFunType : %d", gattFunType);

    bool result = true;
    nsresult rv;

    if(!sBluetoothGattInterface)
    {
        LOGE("sBluetoothGattInterface is null");
        result = false;
    }
    else
    {
        LOGI("xxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
        //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
        switch(gattFunType)
        {
        case BleFunType_RegisterClient:
        {
            //bleGattPara'size ------ BluetoothBleManager::RegisterClient 1
            if(1 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            result = RegisterClient(bleGattPara[0]);
            break;
        }
        case BleFunType_unRegisterClient:
        {
            //bleGattPara'size ------ BluetoothBleManager::UnRegisterClient 1
            if(1 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            int curClientIf = bleGattPara[0].ToInteger(&rv);

            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }
            result = UnRegisterClient(mClientIf);
            break;
        }
        case BleFunType_scanDevice:
        {
            //bleGattPara'size ------ BluetoothBleManager::ScanLEDevice 2
            if(2 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed! client_if:%d", mClientIf);
                mClientIf = curClientIf;
                LOGI("client_if changed! client_if:%d", mClientIf);
            }

            bool start = (bleGattPara[1].EqualsLiteral("1")) ? true : false;
            result = ScanLEDevice(mClientIf, start);
            break;
        }
        case BleFunType_connectBle:
        {
            //bleGattPara'size ------ BluetoothBleManager::ConnectBle 3
            if(3 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            StringToBdAddressType(bleGattPara[1], &mBtBdaddr);
            bool is_direct = (bleGattPara[2].EqualsLiteral("1")) ? true : false;
            result = ConnectBle(mClientIf, &mBtBdaddr, is_direct);
            break;
        }
        case BleFunType_disConnectBle:
        {
            //bleGattPara'size ------ BluetoothBleManager::DisconnectBle 3
            if(3 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            StringToBdAddressType(bleGattPara[1], &mBtBdaddr);

            int curConnId = bleGattPara[2].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            result = DisconnectBle(mClientIf, &mBtBdaddr, mConnId);
            break;
        }
        case BleFunType_setListen:
        {
            //bleGattPara'size ------ BluetoothBleManager::SetListen 2
            if(2 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            bool is_start = (bleGattPara[1].EqualsLiteral("1")) ? true : false;
            result = SetListen(mClientIf, is_start);
            break;
        }
        case BleFunType_refresh:
        {
            //bleGattPara'size ------ BluetoothBleManager::Refresh 2
            if(2 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            StringToBdAddressType(bleGattPara[1], &mBtBdaddr);

            Refresh(mClientIf, &mBtBdaddr);
            break;
        }
        case BleFunType_searchService:
        {
            //bleGattPara'size ------ BluetoothBleManager::SearchService 2
            if(2 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString filterUuid = bleGattPara[1];
            if(filterUuid.EqualsLiteral(""))
            {
                LOGI("BluetoothGatt SearchService filter uuid is null");
                result = SearchService(mConnId, NULL);
            }
            else
            {
                LOGI("BluetoothGatt SearchService filter uuid is not null");

                StringToUuid(filterUuid, &mFilterUuid);
                result = SearchService(mConnId, &mFilterUuid);
            }


            break;
        }
        case BleFunType_getIncludeService:
        {
            //bleGattPara'size ------ BluetoothBleManager::GetIncludeService : 7
            if(7 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];

            if(curUuid.EqualsLiteral(""))
            {
                LOGI("BluetoothGatt getIncludeService charid is null");
                result = GetIncludeService(mConnId, &mSrvcId, NULL);
            }
            else
            {
                LOGI("BluetoothGatt getIncludeService charid is not null");
                StringToUuid(curUuid, &mInclSrvcId.id.uuid);
                mInclSrvcId.id.inst_id = bleGattPara[5].ToInteger(&rv);
                mInclSrvcId.is_primary = bleGattPara[6].ToInteger(&rv);
                result = GetIncludeService(mConnId, &mSrvcId, &mInclSrvcId);
            }

            break;
        }
        case BleFunType_getCharacteristic:
        {
            //bleGattPara'size ------ BluetoothBleManager::GetCharacteristic : 6
            if(6 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];

            if(curUuid.EqualsLiteral(""))
            {
                LOGI("BluetoothGatt getCharacteristic charid is null");
                result = GetCharacteristic(mConnId, &mSrvcId, NULL);
            }
            else
            {
                LOGI("BluetoothGatt getCharacteristic charid is not null");
                StringToUuid(curUuid, &mCharId.uuid);
                mCharId.inst_id = bleGattPara[5].ToInteger(&rv);
                result = GetCharacteristic(mConnId, &mSrvcId, &mCharId);
            }

            break;
        }
        case BleFunType_getDescriptor:
        {
            //bleGattPara'size ------ BluetoothBleManager::GetDescriptor 8
            if(8 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[5].ToInteger(&rv);

            curUuid = bleGattPara[6];
            if(curUuid.EqualsLiteral(""))
            {
                LOGI("BluetoothGatt getDescriptor charid is null");
                result = GetDescriptor(mConnId, &mSrvcId, &mCharId, NULL);
            }
            else
            {
                LOGI("BluetoothGatt getDescriptor charid is not null");
                StringToUuid(curUuid, &mDescrId.uuid);
                mDescrId.inst_id = bleGattPara[7].ToInteger(&rv);
                result = GetDescriptor(mConnId, &mSrvcId, &mCharId, &mDescrId);
            }
            break;
        }
        case BleFunType_readCharacteristic:
        {
            //bleGattPara'size ------ BluetoothBleManager::ReadCharacteristic : 7
            if(7 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[5].ToInteger(&rv);

            int auth_req = bleGattPara[6].ToInteger(&rv);

            result = ReadCharacteristic(mConnId, &mSrvcId, &mCharId, auth_req);

            break;
        }
        case BleFunType_writeCharacteristic:
        {
            //bleGattPara'size ------ BluetoothBleManager::WriteCharacteristic : 10
            if(10 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[5].ToInteger(&rv);

            int write_type = bleGattPara[6].ToInteger(&rv);
            int len = bleGattPara[7].ToInteger(&rv);
            int auth_req = bleGattPara[8].ToInteger(&rv);

            NS_ConvertUTF16toUTF8 pDataValue(bleGattPara[9]);
            const char* p_src_value = pDataValue.get();

            uint8_t value[MAX_HEX_VAL_STR_LEN];
            len = strlen(p_src_value);
            scan_field(p_src_value, len, value, sizeof(value));

            len = (len + 1) / 2;

            LOGI("WriteCharacteristic src_data:%s dest_data:%s len:%d",p_src_value, value, len);

            result = WriteCharacteristic(mConnId, &mSrvcId, &mCharId,
                    write_type, len, auth_req, (char *)value);

            break;
        }
        case BleFunType_readDescriptor:
        {
            //bleGattPara'size ------ BluetoothBleManager::ReadDescriptor 9
            if(9 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }
            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[5].ToInteger(&rv);

            curUuid = bleGattPara[6];
            StringToUuid(curUuid, &mDescrId.uuid);
            mDescrId.inst_id = bleGattPara[7].ToInteger(&rv);

            int auth_req = bleGattPara[8].ToInteger(&rv);

            result = ReadDescriptor(mConnId, &mSrvcId, &mCharId, &mDescrId, auth_req);

            break;
        }
        case BleFunType_writeDescriptor:
        {
            //bleGattPara'size ------ BluetoothBleManager::WriteDescriptor 12
            if(12 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("curConnId changed!");
                mConnId = curConnId;
            }

            nsString curUuid = bleGattPara[1];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[2].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[3].ToInteger(&rv);

            curUuid = bleGattPara[4];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[5].ToInteger(&rv);

            curUuid = bleGattPara[6];
            StringToUuid(curUuid, &mDescrId.uuid);
            mDescrId.inst_id = bleGattPara[7].ToInteger(&rv);

            int write_type = bleGattPara[8].ToInteger(&rv);
            int len = bleGattPara[9].ToInteger(&rv);
            int auth_req = bleGattPara[10].ToInteger(&rv);

            NS_ConvertUTF16toUTF8 pDataValue(bleGattPara[11]);
            const char* p_src_value = pDataValue.get();
            LOGI("===== This is a test =====");
            uint8_t p_tar_value[MAX_HEX_DESCRIPTOR_VAL_STR_LEN];
            len = strlen(p_src_value);
            hex2bin(p_src_value, p_tar_value);
            LOGI("tar: %d,%d", p_tar_value[0], p_tar_value[1]);
            LOGI("===== This is a test =====");

            len = (len + 1) / 2;
            LOGI("WriteCharacteristic src_data:%s dest_data:%s len:%d write_type:%d auth_req:%d",p_src_value, p_tar_value, len, write_type, auth_req);

            result = WriteDescriptor(mConnId, &mSrvcId, &mCharId, &mDescrId,
                    write_type, len, auth_req, (char *)p_tar_value);
            break;
        }
        case BleFunType_executeWrite:
        {
            //bleGattPara'size ------ BluetoothBleManager::ExecuteWrite 2
            if(2 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            int curConnId = bleGattPara[0].ToInteger(&rv);
            if(curConnId != mConnId)
            {
                LOGI("mConnId changed!");
                mConnId = curConnId;
            }

            mExecute = bleGattPara[1].ToInteger(&rv);
            result = ExecuteWrite(mConnId, mExecute);

            break;
        }
        case BleFunType_registerForNotification:
        {
            //bleGattPara'size ------ BluetoothBleManager::RegisterForNotification 7
            if(7 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            StringToBdAddressType(bleGattPara[1], &mBtBdaddr);

            nsString curUuid = bleGattPara[2];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[3].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[4].ToInteger(&rv);

            curUuid = bleGattPara[5];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[6].ToInteger(&rv);

            result = RegisterForNotification(mClientIf, &mBtBdaddr, &mSrvcId, &mCharId);

            break;
        }
        case BleFunType_deregisterForNotification:
        {
            //bleGattPara'size ------ BluetoothBleManager::deRegisterForNotification 7
            if(7 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }
            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            StringToBdAddressType(bleGattPara[1], &mBtBdaddr);

            nsString curUuid = bleGattPara[2];

            StringToUuid(curUuid, &mSrvcId.id.uuid);
            mSrvcId.id.inst_id = bleGattPara[3].ToInteger(&rv);
            mSrvcId.is_primary = bleGattPara[4].ToInteger(&rv);

            curUuid = bleGattPara[5];
            StringToUuid(curUuid, &mCharId.uuid);
            mCharId.inst_id = bleGattPara[6].ToInteger(&rv);

            result = DeregisterForNotification(mClientIf, &mBtBdaddr, &mSrvcId, &mCharId);

            break;
        }
        case BleFunType_readRemoteRssi:
        {
            //bleGattPara'size ------ BluetoothBleManager::ReadRemoteRssi 2
            if(2 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int curClientIf = bleGattPara[0].ToInteger(&rv);
            if(curClientIf != mClientIf)
            {
                LOGI("client_if changed!");
                mClientIf = curClientIf;
            }

            StringToBdAddressType(bleGattPara[1], &mBtBdaddr);

            result = ReadRemoteRssi(mClientIf, &mBtBdaddr);
            break;
        }
        case BleFunType_setAdvData:
        {
            //bleGattPara'size ------ BluetoothBleManager::SetAdvData 9
            if(9 != bleGattPara.Length())
            {
                LOGE("The para size is wrong!");
                return false;
            }

            int server_if = bleGattPara[0].ToInteger(&rv);
            bool set_scan_rsp = (bleGattPara[1].EqualsLiteral("1")) ? true : false;
            bool include_name = (bleGattPara[2].EqualsLiteral("1")) ? true : false;
            bool include_txpower = (bleGattPara[3].EqualsLiteral("1")) ? true : false;
            int min_interval = bleGattPara[4].ToInteger(&rv);
            int max_interval = bleGattPara[5].ToInteger(&rv);
            int appearance = bleGattPara[6].ToInteger(&rv);
            uint16_t manufacturer_len = bleGattPara[7].ToInteger(&rv);

            NS_ConvertUTF16toUTF8 pDataValue(bleGattPara[11]);
            int size = sizeof(pDataValue.get());
            char* manufacturer_data = (char *)calloc(size, sizeof(char));
            memcpy(manufacturer_data, pDataValue.get(), size * sizeof(char));

            result = SetAdvData(server_if, set_scan_rsp, include_name,
                    include_txpower, min_interval, max_interval, appearance,
                    manufacturer_len, manufacturer_data);

            free(manufacturer_data);
            break;
        }
        default:
            break;
        }

        result = true;
    }

    LOGW("temp line:%d", __LINE__);

    return result;
}

bool
BluetoothGatt::SendCallbackSignal(BluetoothSignal &signal)
{
    BluetoothService* bs = BluetoothService::Get();
    if(bs)
    {
        bs->DistributeSignal(signal);
        LOGI("Send callback success");
        return true;
    }
    else
    {
        LOGE("BluetoothService is null");
        return false;
    }
}

/** Registers a GATT client application with the stack */
bool
BluetoothGatt::RegisterClient(nsString uuid)
{
    LOGI("BluetoothGatt RegisterClient");

    bool result = true;

    bt_uuid_t btUuid;
    StringToUuid(uuid, &btUuid);

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->register_client(&btUuid))
    {
        LOGE("Start BluetoothGatt RegisterClient success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt RegisterClient failed");
        result = false;
    }

    return result;
}

void
BluetoothGatt::ProcessRegisterClient(int status, int client_if, bt_uuid_t *app_uuid)
{
    LOGI("callback registerclient start");
    mStatus = status;
    memcpy(&mBtUuid.uu, app_uuid->uu, sizeof(bt_uuid_t));
    mClientIf = client_if;

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_REGISTER_CLIENT_ID));
    LOGW("temp line:%d", __LINE__);
    return;
}

void
BluetoothGatt::SendRegisterClientCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_REGISTER_CLIENT_ID);
    nsString data_statu;
    data_statu.AppendInt(mStatus);
    nsString data_client_if;
    data_client_if.AppendInt(mClientIf);
    nsString data_uuid;
    BtUuidToString(&mBtUuid, data_uuid);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_statu));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CLIENTIF), data_client_if));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_UUID), data_uuid));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback registerclient end");
}

/** unRegisters a GATT client application with the stack */
bool
BluetoothGatt::UnRegisterClient(int client_if)
{
    LOGI("BluetoothGatt UnRegisterClient, uuid : %d", client_if);

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->unregister_client(client_if))
    {
        LOGE("Start BluetoothGatt UnRegisterClient success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt UnRegisterClient failed");
        result = false;
    }

    return result;
}

/** Start or stop LE device scanning */
bool
BluetoothGatt::ScanLEDevice(int client_if, bool start)
{
    LOGI("BluetoothGatt ScanLEDevice, client_if : %d", client_if);

    mGattDevicesMap.clear(); //reset gatt devices map

    bool result = true;

    if(start)
    {
        LOGI("start scan.......................");
    }
    else
    {
        LOGI("stop scan.......................");
    }

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->scan(client_if, start))
    {
        LOGE("Start BluetoothGatt ScanLEDevice success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt ScanLEDevice failed");
        result = false;
    }

    return result;
}

/** Callback for scan results */
void
BluetoothGatt::ProcessScanLEDevice(bt_bdaddr_t* bda, int rssi, uint8_t* adv_data)
{
    LOGI("callback ProcessScanLEDevice start");
    BdAddressTypeToString(bda, mDeviceAddr);
    mRssi = rssi;
    mAdvData = adv_data;

    std::map<nsString, nsString>::iterator iter = mGattDevicesMap.find(mDeviceAddr);
    if(iter != mGattDevicesMap.end())   //jude the gatt device is existed or not
    {
        LOGI("The device is existed");
        return;
    }

    LOGI("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^adv_data size : %d", sizeof(adv_data));

    uint8_t remote_name_len;
    uint8_t *p_eir_remote_name=NULL;
    bt_bdname_t bdname;

    p_eir_remote_name = CheckEirData(adv_data,
            BT_EIR_COMPLETE_LOCAL_NAME_TYPE, &remote_name_len);

    if(p_eir_remote_name == NULL)
    {
        p_eir_remote_name = CheckEirData(adv_data,
                BT_EIR_SHORTENED_LOCAL_NAME_TYPE, &remote_name_len);
    }
    if(p_eir_remote_name == NULL)
    {
        char str[46] = {0};
        p_eir_remote_name = CheckBeaconData(adv_data,
                BT_EIR_MANUFACTURER_SPECIFIC_TYPE, &remote_name_len, str);
    }

    if(p_eir_remote_name)
    {
        memcpy(bdname.name, p_eir_remote_name, remote_name_len);
        bdname.name[remote_name_len]='\0';
        mDeviceName = NS_ConvertUTF8toUTF16(((char*)bdname.name));
    }
    else
    {
        mDeviceName = NS_ConvertUTF8toUTF16("Unknow");
    }

    mDeviceType = sBluetoothGattInterface->client->get_device_type(bda);

    LOGI("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^send scan call back");
    mGattDevicesMap.insert(std::map<nsString, nsString>::value_type(mDeviceAddr, mDeviceName));
    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
            NS_LITERAL_STRING(BLEGATT_SCAN_RESULT_ID));
}

void
BluetoothGatt::SendScanLEDeviceCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("temp line:%d  mRssi:%d", __LINE__, mRssi);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_SCAN_RESULT_ID);
    nsString data_bdaddr;
    data_bdaddr = mDeviceAddr;
    nsString data_rssi;
    data_rssi.AppendInt(mRssi);
    nsString data_device_type;
    data_device_type.AppendInt(mDeviceType);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_BDA), data_bdaddr));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_RSSI), data_rssi));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_ADVDATA), mDeviceName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DEVICE_TYPE), data_device_type));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessScanLEDevice end");
}

/** Create a connection to a remote LE or dual-mode device */
bool
BluetoothGatt::ConnectBle(int client_if, bt_bdaddr_t *bd_addr, bool is_direct)
{
    LOGI("BluetoothGatt ConnectBle, uuid : %d", client_if);

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->connect(client_if, bd_addr, is_direct))
    {
        LOGE("Start BluetoothGatt ConnectBle success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt ConnectBle failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessConnectBle(int conn_id, int status, int client_if, bt_bdaddr_t* bda)
{
    LOGI("callback ProcessConnectBle start");

    mConnectBleConnCommPara.connId = conn_id;
    mConnectBleConnCommPara.status = status;
    mConnectBleConnCommPara.clientIf = client_if;
    memcpy(&mBdaddr, bda, sizeof(bt_bdaddr_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_CONNECT_BLE_ID));
}
void
BluetoothGatt::SendConnectBleCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("temp line:%d  mStatus:%d", __LINE__, mStatus);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_CONNECT_BLE_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(mConnectBleConnCommPara.connId);
    nsString data_status;
    data_status.AppendInt(mConnectBleConnCommPara.status);
    nsString data_client_if;
    data_client_if.AppendInt(mConnectBleConnCommPara.clientIf);
    nsString data_bda;
    BdAddressTypeToString(&mBdaddr, data_bda);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CLIENTIF), data_client_if));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_BDA), data_bda));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessConnectBle end");
}

bool
BluetoothGatt::DisconnectBle(int client_if, bt_bdaddr_t *bd_addr, int conn_id)
{
    LOGI("BluetoothGatt DisconnectBle");

    LOGW(" DisconnectBle temp line:%d  client_if:%d  conn_id:%d", __LINE__, client_if, conn_id);

    bool result = true;

    sBluetoothGattInterface->client->refresh(client_if, bd_addr);

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->disconnect(client_if, bd_addr, conn_id))
    {
        LOGE("Start BluetoothGatt DisconnectBle success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt DisconnectBle failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessDisconnectBle(int conn_id, int status, int client_if, bt_bdaddr_t* bda)
{
    LOGI("callback ProcessDisconnectBle start");

    mDisConnectBleConnCommPara.connId = conn_id;
    mDisConnectBleConnCommPara.status = status;
    mDisConnectBleConnCommPara.clientIf = client_if;
    memcpy(&mBdaddr, bda, sizeof(bt_bdaddr_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_DISCONNECT_BLE_ID));
}
void
BluetoothGatt::SendDisconnectBleCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("temp line:%d", __LINE__);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_DISCONNECT_BLE_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(mDisConnectBleConnCommPara.connId);
    nsString data_status;
    data_status.AppendInt(mDisConnectBleConnCommPara.status);
    nsString data_client_if;
    data_client_if.AppendInt(mDisConnectBleConnCommPara.clientIf);
    nsString data_bda;
    BdAddressTypeToString(&mBdaddr, data_bda);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CLIENTIF), data_client_if));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_BDA), data_bda));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessDisConnectBle end");
}

/** Start or stop advertisements to listen for incoming connections */
bool
BluetoothGatt::SetListen(int client_if, bool start)
{
    LOGI("BluetoothGatt Listen");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->listen(client_if, start))
    {
        LOGE("Start BluetoothGatt Listen success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt Listen failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessListen(int status, int server_if)
{
    LOGI("callback ProcessListen start");

    mListenConnCommPara.status = status;
    mListenConnCommPara.serverIf = server_if;

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_BLE_LISTEN_ID));
}
void
BluetoothGatt::SendListenCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("temp line:%d", __LINE__);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_BLE_LISTEN_ID);
    nsString data_status;
    data_status.AppendInt(mListenConnCommPara.status);
    nsString data_server_if;
    data_server_if.AppendInt(mListenConnCommPara.serverIf);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SERVERIF), data_server_if));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessListen end");
}

/** Clear the attribute cache for a given device */
bool
BluetoothGatt::Refresh(int client_if, const bt_bdaddr_t *bd_addr)
{
    LOGI("BluetoothGatt Refresh");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->refresh(client_if, bd_addr))
    {
        LOGE("Start BluetoothGatt Refresh success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt Refresh failed");
        result = false;
    }

    return result;
}

/**
 * Enumerate all GATT services on a connected device.
 * Optionally, the results can be filtered for a given UUID.
 */
bool
BluetoothGatt::SearchService(int conn_id, bt_uuid_t *btUuid) //nsString filterUuid)
{
    LOGI("BluetoothGatt SearchService");

    bool result = true;
    mGattServiceList.clear();

    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->search_service(conn_id, btUuid))  //sBluetoothGattInterface->client->search_service(conn_id, NULL)
    {
        LOGE("Start BluetoothGatt SearchService success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt SearchService failed");
        result = false;
    }

    return result;
}

void
BluetoothGatt::ProcessSearchResult(int conn_id, btgatt_srvc_id_t *srvc_id)
{
    LOGI("callback ProcessSearchResult start");

    mConnId = conn_id;
    memcpy(&mSrvcId, srvc_id, sizeof(btgatt_srvc_id_t));

    mGattServiceList.push_back(mSrvcId);
}

void
BluetoothGatt::ProcessSearchComplete(int conn_id, int status)
{
    LOGI("callback ProcessSearchComplete start");

    mSearchCompleteConnCommPara.connId = conn_id;
    mSearchCompleteConnCommPara.status = status;

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_SEARCH_COMPLETE_ID));
}

void
BluetoothGatt::SendSearchCompleteCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_SEARCH_COMPLETE_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(mSearchCompleteConnCommPara.connId);
    nsString data_status;
    data_status.AppendInt(mSearchCompleteConnCommPara.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    for(int i = 0, count = mGattServiceList.size(); i < count; ++i)
    {
        SendSearchResultCallback(mSearchCompleteConnCommPara.connId, mGattServiceList[i]);
    }

    LOGI("callback ProcessSearchComplete end");
}

void
BluetoothGatt::SendSearchResultCallback(int conn_id, btgatt_srvc_id_t srvc_id)
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("temp line:%d", __LINE__);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_SEARCH_RESULT_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(conn_id);
    nsString data_srvc_id_id_uuid;
    BtUuidToString(&srvc_id.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(srvc_id.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(srvc_id.is_primary);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessSearchResult end");
}

/**
 * Enumerate included services for a given service.
 * Set start_incl_srvc_id to NULL to get the first included service.
 */
bool
BluetoothGatt::GetIncludeService(int conn_id, btgatt_srvc_id_t *srvc_id,
                            btgatt_srvc_id_t *start_incl_srvc_id)
{
    LOGI("BluetoothGatt GetIncludeService");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->get_included_service(conn_id, srvc_id, start_incl_srvc_id))
    {
        LOGE("Start BluetoothGatt GetIncludeService success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt GetIncludeService failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessGetIncludeService(int conn_id, int status,
                            btgatt_srvc_id_t *srvc_id, btgatt_srvc_id_t *incl_srvc_id)
{
    LOGI("callback ProcessGetIncludeService start");

    mGetIncludeServiceConnCommPara.connId = conn_id;
    mGetIncludeServiceConnCommPara.status = status;
    memcpy(&mSrvcId, srvc_id, sizeof(btgatt_srvc_id_t));
    memcpy(&mInclSrvcId, incl_srvc_id, sizeof(btgatt_srvc_id_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_GET_INCLUDED_SERVICE_ID));
}
void
BluetoothGatt::SendGetIncludeServiceCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_GET_INCLUDED_SERVICE_ID);

    LOGW("SendGetIncludeServiceCallback mConnId:%d mStatus:%d", mGetIncludeServiceConnCommPara.connId, mGetIncludeServiceConnCommPara.status);

    nsString data_conn_id;
    data_conn_id.AppendInt(mGetIncludeServiceConnCommPara.connId);

    nsString data_status;
    data_status.AppendInt(mGetIncludeServiceConnCommPara.status);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mSrvcId.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mSrvcId.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mSrvcId.is_primary);

    nsString data_incl_srvc_id_id_uuid;
    BtUuidToString(&mInclSrvcId.id.uuid, data_incl_srvc_id_id_uuid);
    nsString data_incl_srvc_id_id_inst_id;
    data_incl_srvc_id_id_inst_id.AppendInt(mInclSrvcId.id.inst_id);
    nsString data_incl_srvc_id_is_primary;
    data_incl_srvc_id_is_primary.AppendInt(mInclSrvcId.is_primary);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_INCLSRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_INCLSRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_INCLSRVCID_ISPRIMARY), data_srvc_id_is_primary));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessGetIncludeService end");
}

/**
 * Enumerate characteristics for a given service.
 * Set start_char_id to NULL to get the first characteristic.
 */
bool
BluetoothGatt::GetCharacteristic(int conn_id, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *start_char_id)
{
    LOGI("BluetoothGatt GetCharacteristic");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->get_characteristic(conn_id, srvc_id, start_char_id))
    {
        LOGE("Start BluetoothGatt GetCharacteristic success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt GetCharacteristic failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessGetCharacteristic(int conn_id, int status,
                            btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id, int char_prop)
{
    LOGI("callback ProcessGetCharacteristic start");

    mGetCharacteristicConnCommPara.connId = conn_id;
    mGetCharacteristicConnCommPara.status = status;
    memcpy(&mSrvcId, srvc_id, sizeof(btgatt_srvc_id_t));
    memcpy(&mCharId, char_id, sizeof(btgatt_gatt_id_t));
    mCharProp = char_prop;

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_GET_CHRACTERISTIC_ID));
}
void
BluetoothGatt::SendGetCharacteristicCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_GET_CHRACTERISTIC_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(mGetCharacteristicConnCommPara.connId);
    nsString data_status;
    data_status.AppendInt(mGetCharacteristicConnCommPara.status);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mSrvcId.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mSrvcId.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mSrvcId.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mCharId.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mCharId.inst_id);

    nsString data_char_prop;
    data_char_prop.AppendInt(mCharProp);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHAR_PROP), data_char_prop));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessGetCharacteristic end");
}

/**
 * Enumerate descriptors for a given characteristic.
 * Set start_descr_id to NULL to get the first descriptor.
 */
bool
BluetoothGatt::GetDescriptor(int conn_id, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id,
                             btgatt_gatt_id_t *start_descr_id)
{
    LOGI("BluetoothGatt GetDescriptor");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->get_descriptor(conn_id, srvc_id, char_id,
                                                             start_descr_id))
    {
        LOGE("Start BluetoothGatt GetDescriptor success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt GetDescriptor failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessGetDescriptor(int conn_id, int status,
                        btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id,
                        btgatt_gatt_id_t *descr_id)
{
    LOGI("callback ProcessGetDescriptor start");

    mGetDescriptorConnCommPara.connId = conn_id;
    mGetDescriptorConnCommPara.status = status;
    LOGI("############### readdescrip uuid mConnId:%d mStatus:%d", conn_id, status);
    LOGI("############### readdescrip uuid mConnId:%d mStatus:%d", mGetDescriptorConnCommPara.connId, mGetDescriptorConnCommPara.status);
    memcpy(&mSrvcId, srvc_id, sizeof(btgatt_srvc_id_t));
    memcpy(&mCharId, char_id, sizeof(btgatt_gatt_id_t));
    memcpy(&mDescrId, descr_id, sizeof(btgatt_gatt_id_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_GET_DESCRIPTOR_ID));
}
void
BluetoothGatt::SendGetDescriptorCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("SendGetDescriptorCallback mConnId:%d mStatus:%d", mGetDescriptorConnCommPara.connId, mGetDescriptorConnCommPara.status);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_GET_DESCRIPTOR_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(mGetDescriptorConnCommPara.connId);
    nsString data_status;
    data_status.AppendInt(mGetDescriptorConnCommPara.status);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mSrvcId.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mSrvcId.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mSrvcId.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mCharId.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mCharId.inst_id);

    nsString data_descr_id_uuid;
    BtUuidToString(&mDescrId.uuid, data_descr_id_uuid);
    nsString data_descr_id_inst_id;
    data_descr_id_inst_id.AppendInt(mDescrId.inst_id);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_UUID), data_descr_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_INSTID), data_descr_id_inst_id));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessGetDescriptor end");
}

/** Read a characteristic on a remote device */
bool
BluetoothGatt::ReadCharacteristic(int conn_id, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id, int auth_req)
{
    LOGI("BluetoothGatt ReadCharacteristic");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->read_characteristic(conn_id, srvc_id, char_id, auth_req))
    {
        LOGE("Start BluetoothGatt ReadCharacteristic success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt ReadCharacteristic failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessReadCharacteristic(int conn_id, int status, btgatt_read_params_t *p_data)
{
    LOGI("callback ProcessReadCharacteristic start");

    mReadCharacteristicConnCommPara.connId = conn_id;
    mReadCharacteristicConnCommPara.status = status;
    memcpy(&mReadParaData, p_data, sizeof(btgatt_read_params_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_READ_CHARACTERISTIC_ID));
}
void
BluetoothGatt::SendReadCharacteristicCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_READ_CHARACTERISTIC_ID);

    LOGW("SendReadCharacteristicCallback mConnId:%d mStatus:%d", mReadCharacteristicConnCommPara.connId, mReadCharacteristicConnCommPara.status);

    nsString data_conn_id;
    data_conn_id.AppendInt(mReadCharacteristicConnCommPara.connId);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mReadParaData.srvc_id.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mReadParaData.srvc_id.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mReadParaData.srvc_id.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mReadParaData.char_id.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mReadParaData.char_id.inst_id);

    nsString data_descr_id_uuid;
    BtUuidToString(&mReadParaData.descr_id.uuid, data_descr_id_uuid);
    nsString data_descr_id_inst_id;
    data_descr_id_inst_id.AppendInt(mReadParaData.descr_id.inst_id);

    LOGI("^^^^^^^^^^^^^^^^^^^^^ btgatt_read_params_t len:%d", mReadParaData.value.len);
    nsString data_value;
    char strValue[MAX_HEX_VAL_STR_LEN];
    array2str(mReadParaData.value.value, mReadParaData.value.len, strValue, sizeof(strValue));

    LOGI("############### strValue:%s", strValue);
    data_value = NS_ConvertUTF8toUTF16(strValue);

    nsString data_value_type;
    data_value_type.AppendInt(mReadParaData.descr_id.inst_id);

    nsString data_status;
    data_status.AppendInt(mReadCharacteristicConnCommPara.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_UUID), data_descr_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_INSTID), data_descr_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_VALUE), data_value));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_VALUE_TYPE), data_value_type));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));


    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessReadCharacteristic end");
}

/** Write a remote characteristic */
bool
BluetoothGatt::WriteCharacteristic(int conn_id, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id,
                            int write_type, int len, int auth_req, char* p_value)
{
    LOGI("BluetoothGatt WriteCharacteristic");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->write_characteristic(conn_id, srvc_id, char_id,
                                                write_type, len, auth_req, p_value))
    {
        LOGE("Start BluetoothGatt WriteCharacteristic success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt WriteCharacteristic failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessWriteCharacteristic(int conn_id, int status, btgatt_write_params_t *p_data)
{
    LOGI("callback ProcessWriteCharacteristic start");

    mWriteCharacteristicConnCommPara.connId = conn_id;
    mWriteCharacteristicConnCommPara.status = status;
    memcpy(&mWriteParaData, p_data, sizeof(btgatt_write_params_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_WRITER_HARACTERISTIC_ID));
}
void
BluetoothGatt::SendWriteCharacteristicCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_WRITER_HARACTERISTIC_ID);

    LOGW("SendWriteCharacteristicCallback mConnId:%d mStatus:%d", mWriteCharacteristicConnCommPara.connId, mWriteCharacteristicConnCommPara.status);

    nsString data_conn_id;
    data_conn_id.AppendInt(mWriteCharacteristicConnCommPara.connId);

    nsString data_status;
    data_status.AppendInt(mWriteCharacteristicConnCommPara.status);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mWriteParaData.srvc_id.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mWriteParaData.srvc_id.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mWriteParaData.srvc_id.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mWriteParaData.char_id.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mWriteParaData.char_id.inst_id);

    nsString data_descr_id_uuid;
    BtUuidToString(&mWriteParaData.descr_id.uuid, data_descr_id_uuid);
    nsString data_descr_id_inst_id;
    data_descr_id_inst_id.AppendInt(mWriteParaData.descr_id.inst_id);

    LOGI("btgatt_write_params_t status:%d", mWriteParaData.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_UUID), data_descr_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_INSTID), data_descr_id_inst_id));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessWriteCharacteristic end");
}

/** Read the descriptor for a given characteristic */
bool
BluetoothGatt::ReadDescriptor(int conn_id, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id,
                        btgatt_gatt_id_t *descr_id, int auth_req)
{
    LOGI("BluetoothGatt ReadDescriptor");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->read_descriptor(conn_id, srvc_id, char_id,
                                            descr_id, auth_req))
    {
        LOGE("Start BluetoothGatt ReadDescriptor success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt ReadDescriptor failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessReadDescriptor(int conn_id, int status, btgatt_read_params_t *p_data)
{
    LOGI("callback ProcessReadDescriptor start");

    mReadDescriptorConnCommPara.connId = conn_id;
    mReadDescriptorConnCommPara.status = status;
    memcpy(&mReadParaData, p_data, sizeof(btgatt_read_params_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_READ_DESCRIPTOR_ID));
}
void BluetoothGatt::SendReadDescriptorCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_READ_DESCRIPTOR_ID);

    LOGW("SendReadDescriptorCallback mConnId:%d mStatus:%d", mReadDescriptorConnCommPara.connId, mReadDescriptorConnCommPara.status);

    nsString data_conn_id;
    data_conn_id.AppendInt(mReadDescriptorConnCommPara.connId);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mReadParaData.srvc_id.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mReadParaData.srvc_id.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mReadParaData.srvc_id.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mReadParaData.char_id.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mReadParaData.char_id.inst_id);

    nsString data_descr_id_uuid;
    BtUuidToString(&mReadParaData.descr_id.uuid, data_descr_id_uuid);
    nsString data_descr_id_inst_id;
    data_descr_id_inst_id.AppendInt(mReadParaData.descr_id.inst_id);

    LOGI("^^^^^^^^^^^^^^^^^^^^^ btgatt_read_params_t len:%d", mReadParaData.value.len);
    nsString data_value;
    char strValue[MAX_HEX_VAL_STR_LEN];
    array2str(mReadParaData.value.value, mReadParaData.value.len, strValue, sizeof(strValue));

    LOGI("############### readdescrip strValue:%s", strValue);
    data_value = NS_ConvertUTF8toUTF16(strValue);

    nsString data_value_type;
    data_value_type.AppendInt(mReadParaData.descr_id.inst_id);

    nsString data_status;
    data_status.AppendInt(mReadDescriptorConnCommPara.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_UUID), data_descr_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_INSTID), data_descr_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_VALUE), data_value));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_VALUE_TYPE), data_value_type));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));


    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessReadDescriptor end");
}

/** Write a remote descriptor for a given characteristic */
bool
BluetoothGatt::WriteDescriptor(int conn_id, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id,
        btgatt_gatt_id_t *descr_id, int write_type, int len,
        int auth_req, char* p_value)
{
    LOGI("BluetoothGatt WriteDescriptor");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->write_descriptor(conn_id, srvc_id, char_id, descr_id,
            write_type, len, auth_req, p_value))
    {
        LOGE("Start BluetoothGatt WriteDescriptor success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt WriteDescriptor failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessWriteDescriptor(int conn_id, int status, btgatt_write_params_t *p_data)
{
    LOGI("callback ProcessWriteDescriptor start");

    mWriteDescriptorConnCommPara.connId = conn_id;
    mWriteDescriptorConnCommPara.status = status;
    memcpy(&mWriteParaData, p_data, sizeof(btgatt_write_params_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_WRITE_DESCRIPTOR_ID));
}
void
BluetoothGatt::SendWriteDescriptorCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_WRITE_DESCRIPTOR_ID);

    LOGW("SendWriteDescriptorCallback mConnId:%d mStatus:%d", mWriteDescriptorConnCommPara.connId, mWriteDescriptorConnCommPara.status);

    nsString data_conn_id;
    data_conn_id.AppendInt(mWriteDescriptorConnCommPara.connId);

    nsString data_status;
    data_status.AppendInt(mWriteDescriptorConnCommPara.status);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mWriteParaData.srvc_id.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mWriteParaData.srvc_id.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mWriteParaData.srvc_id.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mWriteParaData.char_id.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mWriteParaData.char_id.inst_id);

    nsString data_descr_id_uuid;
    BtUuidToString(&mWriteParaData.descr_id.uuid, data_descr_id_uuid);
    nsString data_descr_id_inst_id;
    data_descr_id_inst_id.AppendInt(mWriteParaData.descr_id.inst_id);

    LOGI("btgatt_write_params_t status:%d", mWriteParaData.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_UUID), data_descr_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_INSTID), data_descr_id_inst_id));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessWriteDescriptor end");
}

/** Execute a prepared write operation */
bool
BluetoothGatt::ExecuteWrite(int conn_id, int execute)
{
    LOGI("BluetoothGatt ExecuteWrite");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->execute_write(conn_id, execute))
    {
        LOGE("Start BluetoothGatt ExecuteWrite success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt ExecuteWrite failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessExecuteWrite(int conn_id, int status)
{
    LOGI("callback ProcessExecuteWrite start");

    mExecuteConnCommPara.connId = conn_id;
    mExecuteConnCommPara.status = status;

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_EXECUTE_WRITE_ID));
}
void
BluetoothGatt::SendExecuteWriteCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    LOGW("temp line:%d callback ProcessExecuteWrite mConnId:%d mStatus:%d", __LINE__, mExecuteConnCommPara.connId, mExecuteConnCommPara.status);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_EXECUTE_WRITE_ID);
    nsString data_conn_id;
    data_conn_id.AppendInt(mExecuteConnCommPara.connId);
    nsString data_status;
    data_status.AppendInt(mExecuteConnCommPara.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessExecuteWrite end");
}

/**
 * Register to receive notifications or indications for a given
 * characteristic
 */
bool
BluetoothGatt::RegisterForNotification(int client_if, const bt_bdaddr_t *bd_addr, btgatt_srvc_id_t *srvc_id,
        btgatt_gatt_id_t *char_id)
{
    LOGI("BluetoothGatt RegisterForNotification");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->register_for_notification(client_if, bd_addr, srvc_id,
                                                        char_id))
    {
        LOGE("Start BluetoothGatt RegisterForNotification success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt RegisterForNotification failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessRegisterForNotification(int conn_id, int registered, int status, btgatt_srvc_id_t *srvc_id,
        btgatt_gatt_id_t *char_id)
{
    LOGI("callback ProcessRegisterForNotification start");

    mRegForNotiConnCommPara.connId = conn_id;
    mRegistered = registered;
    mRegForNotiConnCommPara.status = status;
    memcpy(&mSrvcId, srvc_id, sizeof(btgatt_srvc_id_t));
    memcpy(&mCharId, char_id, sizeof(btgatt_gatt_id_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_REGISTER_FOR_NOTIFICATION_ID));
}
void
BluetoothGatt::SendRegisterForNotificationCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_REGISTER_FOR_NOTIFICATION_ID);

    nsString data_conn_id;
    data_conn_id.AppendInt(mRegForNotiConnCommPara.connId);

    nsString data_registered;
    data_registered.AppendInt(mRegistered);

    nsString data_status;
    data_status.AppendInt(mRegForNotiConnCommPara.status);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mSrvcId.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mSrvcId.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mSrvcId.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mCharId.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mCharId.inst_id);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_REGISTERED), data_registered));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessRegisterForNotification end");
}

/** Deregister a previous request for notifications/indications */
bool
BluetoothGatt::DeregisterForNotification(int client_if, const bt_bdaddr_t *bd_addr, btgatt_srvc_id_t *srvc_id,
        btgatt_gatt_id_t *char_id)
{
    LOGI("BluetoothGatt DeregisterForNotification");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->deregister_for_notification(client_if,
                                            bd_addr, srvc_id, char_id))
    {
        LOGE("Start BluetoothGatt DeregisterForNotification success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt DeregisterForNotification failed");
        result = false;
    }

    return result;
}

/** Request RSSI for a given remote device */
bool
BluetoothGatt::ReadRemoteRssi(int client_if, const bt_bdaddr_t *bd_addr)
{
    LOGI("BluetoothGatt ReadRemoteRssi");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->read_remote_rssi(client_if, bd_addr))
    {
        LOGE("Start BluetoothGatt ReadRemoteRssi success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt ReadRemoteRssi failed");
        result = false;
    }

    return result;
}
void
BluetoothGatt::ProcessReadRemoteRssi(int client_if, bt_bdaddr_t* bda, int rssi, int status)
{
    LOGI("callback ProcessReadRemoteRssi start");

    mReadRssiConnCommPara.clientIf = client_if;
    memcpy(&mBdaddr, bda, sizeof(bt_bdaddr_t));
    mRssi = rssi;
    mReadRssiConnCommPara.status = status;

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_READ_REMOTERSSI_ID));
}
void
BluetoothGatt::SendReadRemoteRssiCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_READ_REMOTERSSI_ID);

    nsString data_client_if;
    data_client_if.AppendInt(mReadRssiConnCommPara.clientIf);

    nsString data_bda;
    BdAddressTypeToString(&mBdaddr, data_bda);

    nsString data_rssi;
    data_rssi.AppendInt(mRssi);

    nsString data_status;
    data_status.AppendInt(mReadRssiConnCommPara.status);

    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CLIENTIF), data_client_if));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_BDA), data_bda));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_RSSI), data_rssi));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_STATUS), data_status));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);


    LOGI("callback ProcessReadRemoteRssi end");
}

/** Set the advertising data or scan response data */
bool
BluetoothGatt::SetAdvData(int server_if, bool set_scan_rsp, bool include_name,
        bool include_txpower, int min_interval, int max_interval, int appearance,
        uint16_t manufacturer_len, char* manufacturer_data)
{
    LOGI("BluetoothGatt SetAdvData");

    bool result = true;

    //BT_STATUS_SUCCESS-suceess, BT_STATUS_NOMEM-failed
    if(BT_STATUS_SUCCESS == sBluetoothGattInterface->client->set_adv_data(server_if, set_scan_rsp, include_name,
            include_txpower, min_interval, max_interval, appearance,
            manufacturer_len, manufacturer_data))
    {
        LOGE("Start BluetoothGatt SetAdvData success");
        result = true;
    }
    else
    {
        LOGE("Start BluetoothGatt SetAdvData failed");
        result = false;
    }

    return result;
}

void
BluetoothGatt::ProcessNotify(int conn_id, btgatt_notify_params_t *p_data)
{
    LOGI("callback ProcessNotify start");

    mNotifyConnCommPara.connId = conn_id;
    memcpy(&mNotifyParaData, p_data, sizeof(btgatt_notify_params_t));

    BT_HF_DISPATCH_MAIN(MainThreadTaskCmd::NOTIFY_GATT_CALLBACKS,
                            NS_LITERAL_STRING(BLEGATT_NOTIFY_ID));
}
void
BluetoothGatt::SendNotifyCallback()
{
    nsAutoString eventName;
    eventName.AssignLiteral(BLUETOOTH_GATT_CALLBACKS_ID);

    nsAutoString callbackName;
    callbackName.AssignLiteral(BLEGATT_NOTIFY_ID);

    nsString data_conn_id;
    data_conn_id.AppendInt(mNotifyConnCommPara.connId);

//    nsString data_value;
//    data_value.APpendInt(mNotifyParaData);

    nsString data_bdAddr;
    BdAddressTypeToString(&mNotifyParaData.bda, data_bdAddr);

    nsString data_srvc_id_id_uuid;
    BtUuidToString(&mNotifyParaData.srvc_id.id.uuid, data_srvc_id_id_uuid);
    nsString data_srvc_id_id_inst_id;
    data_srvc_id_id_inst_id.AppendInt(mNotifyParaData.srvc_id.id.inst_id);
    nsString data_srvc_id_is_primary;
    data_srvc_id_is_primary.AppendInt(mNotifyParaData.srvc_id.is_primary);

    nsString data_char_id_uuid;
    BtUuidToString(&mNotifyParaData.char_id.uuid, data_char_id_uuid);
    nsString data_char_id_inst_id;
    data_char_id_inst_id.AppendInt(mNotifyParaData.char_id.inst_id);

    nsString data_len;
    data_len.AppendInt(mNotifyParaData.len);

    nsString data_is_notify;
    data_is_notify.AppendInt(mNotifyParaData.is_notify);

    LOGI("^^^^^^^^^^^^^^^^^^^^^ btgatt_notify_params_t len:%d", mNotifyParaData.len);
    nsString data_value;
    char strValue[MAX_HEX_VAL_STR_LEN];
    array2str(mNotifyParaData.value, mNotifyParaData.len, strValue, sizeof(strValue));

    LOGI("############### strValue:%s", strValue);
    data_value = NS_ConvertUTF8toUTF16(strValue);


    InfallibleTArray<BluetoothNamedValue> data;
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CALLBACK_NAME), callbackName));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CONNID), data_conn_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_DESCRID_VALUE), data_value));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_BDA), data_bdAddr));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_UUID), data_srvc_id_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ID_INSTID), data_srvc_id_id_inst_id));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_SRVCID_ISPRIMARY), data_srvc_id_is_primary));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_UUID), data_char_id_uuid));
    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_CHARID_INSTID), data_char_id_inst_id));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_LEN), data_len));

    data.AppendElement(
            BluetoothNamedValue(NS_LITERAL_STRING(GATT_PARA_IS_NOTIFY), data_is_notify));

    BluetoothSignal signal(eventName, NS_LITERAL_STRING(KEY_ADAPTER), data);
    SendCallbackSignal(signal);

    LOGI("callback ProcessNotify end");
}
