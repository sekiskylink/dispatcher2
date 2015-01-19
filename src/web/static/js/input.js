$(function(){
    /* Activity Changed */
    $('#activity').change(function(){
        $('#input_table').empty();
        var activityid = $(this).val();
        if (activityid == '0'){
            return;
        }

        $.get(
            '/ajax_portal',
            {xtype:'quantities', xid: activityid},
            function(data){
                /* get the primary quantities for this activity */
                $('#input_table').append(data);
            }
        );
    });

    /*Give sb preview before they save*/
    $('#preview').click(function(){
        alert('gwe');
        return false;
    });
});
