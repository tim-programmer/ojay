#import "Basic";

render := Log_Section.{name = "render"};
audio  := Log_Section.{name = "audio"};

my_logger :: (message: string, data: *void, info: Log_Info) {
    name := ifx info.section then info.section.name else "-";
    print("[%] %\n", name, message);
}

main :: () {
    context.logger = my_logger;
    log("frame %", 1, section = *render);
    log_error("underrun", section = *audio);
    log("no section");
    context.log_section = *audio;
    log("from the context");
    log("explicit wins", section = *render);
}
