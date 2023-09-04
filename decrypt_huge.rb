require "rubyserial"

serialport = Serial.new "/dev/ttyS0", 115200

class File
    def each_chunk(chunk_size = 1024)
        yield read(chunk_size) until eof?
    end
end

i = 0

File.open("/media/pi/38D0DABAD0DA7D96/osos-n3g.bin", 'wb') do |d|
    File.open("/media/pi/38D0DABAD0DA7D96/ftl-dump.bin", "rb") do |f|
        f.seek(0x4E46800)
        f.each_chunk do |data|
            serialport.write data
            
            data_in = []
            loop do
                byte = serialport.getbyte
                data_in << byte if byte
                break if data_in.length == 1024
            end
            
            d.write data_in.pack("C*")
            i += 1
            print "Processed #{i} chunks\r"
            break if i > 10600
        end
    end
end

serialport.close
