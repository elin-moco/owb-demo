var catEating = false;

function feedCat() {
    var charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, '03A000');
    if (charWritten) {
        setTimeout(function() {
            charWritten = bleManager.writeCharacteristic(conn_id, service_id, char_rx_id, write_type, 6, auth_req, '030000');
            if (charWritten) {
                $.post('http://catfeeder.inspire.mozilla.com.tw/api/feed');
                sendChannel.send('feed');
            }
        }, 1000);
    }
}

window.showBleInput = function (characteristic) {
    var pin = parseInt(characteristic.substr(0, 2), 16);
    var content = parseInt(characteristic.substr(2, 4), 16);
    if (pin == 0x0A) {
        $('#digital').text((content == 0x0100) ? 'On' : 'Off');
    }
    else if (pin == 0x0B) {
        $('#analog').text(content);
        if (!catEating && content > 200) {
            catEating = true;
            $.post('http://catfeeder.inspire.mozilla.com.tw/api/rub', function(response) {
                console.info(response);
                if (response.timeToFeed) {
                    feedCat();
                }
            });
            sendChannel.send('rub');
        }
        else {
            catEating = false;
            $.post('http://catfeeder.inspire.mozilla.com.tw/api/leave');
            sendChannel.send('leave');
        }
    }
};


window.onRtcMessage = function(msg) {
    if ('feed' == msg) {
        feedCat();
    }
};